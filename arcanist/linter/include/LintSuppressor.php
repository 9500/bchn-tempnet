<?php

function isSuppressed($fileContent, $offset, $linter, $rule) {
  $line = getLineAtOffset($fileContent, $offset);
  if (lineHasSuppressComment($line, 'NOLINT', $linter, $rule)) {
    return true;
  }

  $previousLine = getPreviousLineAtOffset($fileContent, $offset);
  if (lineHasSuppressComment($previousLine, 'NOLINTNEXTLINE', $linter, $rule)) {
    return true;
  }

  return false;
}

function lineHasSuppressComment($line, $directive, $linter, $rule) {
  if (!preg_match_all('/\b'.$directive.'\s*\(([^)]*)\)/', $line, $matches)) {
    return false;
  }

  foreach ($matches[1] as $ruleList) {
    # Convert comma-delimited list of rules into an array
    $rules = preg_split('/\s*,\s*/', trim($ruleList));

    foreach ($rules as $r) {
      if ($r === $linter || $r === $linter.$rule) {
        return true;
      }
    }
  }

  return false;
}

function getLineAtOffset($fileContent, $offset) {
  $prefix = substr($fileContent, 0, $offset);
  $lineStart = strrpos($prefix, "\n");
  $lineStart = ($lineStart === false) ? 0 : $lineStart + 1;
  $lineEnd = strpos($fileContent, "\n", $offset);
  $lineEnd = ($lineEnd === false) ? strlen($fileContent) : $lineEnd;
  return substr($fileContent, $lineStart, $lineEnd - $lineStart);
}

function getPreviousLineAtOffset($fileContent, $offset) {
  $prefix = substr($fileContent, 0, $offset);
  $lineStart = strrpos($prefix, "\n");
  if ($lineStart === false) {
    return '';
  }
  $beforeCurrentLine = substr($fileContent, 0, $lineStart);
  $previousLineStart = strrpos($beforeCurrentLine, "\n");
  $previousLineStart = ($previousLineStart === false) ? 0 : $previousLineStart + 1;
  return substr($beforeCurrentLine, $previousLineStart, strlen($beforeCurrentLine) - $previousLineStart);
}
