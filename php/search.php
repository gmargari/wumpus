<?php

	include_once("wumpus.php");
	include_once("configuration.php");

	header("Cache-Control: no-cache");
	header("Pragma: no-cache");

	if ((!isset($_SERVER["REMOTE_ADDR"])) || ($_SERVER["REMOTE_ADDR"] != "127.0.0.1")) {
		setcookie("username", "");
		setcookie("password", "");
		header("Location: login.php");
		exit(0);
	}

	if ((!isset($_COOKIE["username"])) || (!isset($_COOKIE["password"]))) {
		header("Location: login.php");
		exit(0);
	}

	$fileTypes =
		array ( "text/html", "application/pdf", "application/postscript", "text/plain",
		        "application/msword", "text/xml", "text/x-mail", "audio/mpeg",
		        "application/multitext" );
	$typeDescriptions =
		array ( "HTML documents", "Adobe PDF", "Adobe PostScript", "Text files",
		        "Microsoft Office", "XML documents", "E-mail messages", "MPEG audio files",
		        "MultiText input files" );
	for ($i = 0; $i < count($fileTypes); $i++)
		$typeToDescription[$fileTypes[$i]] = $typeDescriptions[$i];
	$typeToDescription["everything"] = "Everything";
	$typeToDescription["email"] = "E-mail messages";
	$typeToDescription["office"] = "Office documents";
	$typeToDescription["files"] = "Files";
	
	$username = $_COOKIE["username"];
	$password = $_COOKIE["password"];

	$wumpus = new WumpusConnection($host, $port, $username, $password);
	if ((substr($wumpus->status, 0, 2) == "@1") || (strlen($wumpus->status) < 2)) {
		header("Location: login.php");
		exit(0);
	}

	$start = 0;
	$end = 10;
	if (isset($_GET["start"]))
		$start = $_GET["start"];
	if (isset($_GET["end"]))
		$end = $_GET["end"];
	if ($start < 0)
		$start = 0;
	if ($end < $start + 10)
		$end = $start + 10;
	if ($end > $start + 20)
		$end = $start + 20;
	if (isset($_GET["filetype"])) {
		$fileType = $_GET["filetype"];
		if ($fileType == "")
			$fileType = "everything";
	}
	else
		$fileType = "everything";
	if (!is_array($fileType))
		$fileType = array ($fileType);
	$typeDescription = "";
	foreach ($fileType as $ft) {
		if (isset($typeToDescription[$ft])) {
			if ($typeDescription == "")
				$typeDescription = $typeToDescription[$ft];
			else
				$typeDescription .= ", " . $typeToDescription[$ft];
		}
	}

	$query = $_GET["query"];
	if (isset($_GET["getfrom"]))
		processGetQuery();
	else if ((isset($_GET["query"])) && ($query != ""))
		processQuery();
	else
		printIndexContents();
	
	exit(0);

	function printHeader($title, $content) {
		global $query, $fileType, $username, $fileTypes, $typeToDescription;
		if (isset($_GET["advanced"])) {
			?>
			<html><head>
				<title>Wumpus File System Search</title>
				<style>
					<!-- body,td,div,.p,a{font-family:arial,sans-serif } //-->
				</style>
			</head>
			<body bgcolor="#FFFFFF">
				<br>
				<table border="0" cellpadding="0" cellspacing="0" width="100%"><tr><td>
					<form action="search.php" method="GET">
					<input type="hidden" name="advanced">
					<table border="0" cellpadding="1" cellspacing="1">
						<tr>
							<td>Search query:</td>
							<td>&nbsp;&nbsp;</td>
							<td>
								<input type="text" name="query" size="64" maxlength="64" value=<?php
								print "\"" . htmlspecialchars($query) . "\"";
								?>>
							</td><td>
								&nbsp; <input type="submit" value="Search"> &nbsp;
							</td>
						</tr><tr valign="top">
							<td align="center"><font size="-1"><a href="logout.php">Logout</a></font></td>
							<td>&nbsp;</td>
							<td>
								<table border="0" cellspacing="0" cellpadding="0" width="100%">
									<tr valign="top"><td><font size="-1">
										<b>Document type</b> <br>
										<select name="filetype[]" size="7" multiple>
<?php
											foreach ($fileTypes as $ft) {
												print "<option value=\"$ft\"";
												if (in_array($ft, $fileType))
													print " selected";
												print ">" . $typeToDescription[$ft] . "</option>\n";
											}
?>
										</select>
									</font></td><td width="20">&nbsp;</td><td align="right">
										<table border="0" cellspacing="0" cellpadding="0"><tr><td><font size="-1">
											<b>File name</b> (arbitrary wildcard expression) <br>
											<input type="text" name="filename" size="40" maxlength="40"<?php
												if (isset($_GET["filename"]))
													print " value=\"" . $_GET["filename"] . "\"";
											?>> <font size="-2"><br><br></font>
											<b>File size</b> (format: <i>MinSize - MaxSize</i>)<br>
											<input type="text" name="filesize" size="40" maxlength="40" value="not implemented yet"> <font size="-2"><br><br></font>
											<b>Modified after</b> (format: <i>YYYY/MM/DD:HH:MM</i>) <br>
											<input type="text" name="modified-after" size="40" maxlength="40" value="not implemented yet">
										</font></td></tr></table>
									</td></tr>
								</table>
							</td><td align="center">
								&nbsp; <font size="-1"><a href="search.php">Standard</a></font> &nbsp;
						</td></tr>
					</table>
					</form>
				</td><td align="right" valign="top">
					<a href="search.php"><img src="wumpus_logo.gif" border="0"></a> <br>
					<font size="-2">Copyright &copy; 2005 by
					<a href="http://stefan.buettcher.org/" target="_blank">Stefan B&uuml;ttcher</a> &nbsp; &nbsp;</font>
				</td></tr></table>
				<br>
				<table width="100%" border="0" cellpadding="0" cellspacing="0">
					<tr><td bgcolor="#4070D0"><img width="1" height="1" alt=""></td></tr>
				</table>
				<table width="100%" border="0" cellpadding="0" cellspacing="0" bgcolor="#e5ecf9">
					<tr><td bgcolor="#E0F0FF" nowrap>
						<font size=+1>&nbsp;<?php print $title; ?></font>&nbsp;
					</td><td bgcolor="#E0F0FF" align="right" nowrap>
						<font size="-1"><?php print $content; ?></font>&nbsp;
					</td></tr>
				</table>
			<?php
		}
		else {
			?>
			<html><head>
				<title>Wumpus File System Search</title>
				<style>
					<!-- body,td,div,.p,a{font-family:arial,sans-serif } //-->
				</style>
			</head>
			<body bgcolor="#FFFFFF">
				<br>
				<table border="0" cellpadding="0" cellspacing="0" width="100%"><tr><td>
					<form action="search.php" method="GET">
					<table border="0" cellpadding="1" cellspacing="1">
						<tr>
							<td>Search query:</td>
							<td>&nbsp;&nbsp;</td>
							<td>
								<input type="text" name="query" size="56" maxlength="64" value=<?php
								print "\"" . htmlspecialchars($query) . "\"";
								?>>
							</td><td>
								&nbsp; <input type="submit" value="Search"> &nbsp;
							</td>
						</tr><tr>
							<td align="center"><font size="-1"><a href="logout.php">Logout</a></font></td>
							<td>&nbsp;</td>
							<td><font size="-1">
								<input id="everything" type="radio" name="filetype" value="everything" <?php
									if (in_array("everything", $fileType)) print "checked"; else print "unchecked" ?>>
									<label for="everything"> everything</label>
								<input id="files" type="radio" name="filetype" value="files" <?php
									if (in_array("files", $fileType)) print "checked"; else print "unchecked" ?>>
									<label for="files"> files</label>
								<input id="email" type="radio" name="filetype" value="text/x-mail" <?php
									if ((in_array("email", $fileType)) || (in_array("text/x-mail", $fileType))) print "checked"; else print "unchecked" ?>>
									<label for="email"> e-mail messages</label>
								<input id="office" type="radio" name="filetype" value="office" <?php
									if (in_array("office", $fileType)) print "checked"; else print "unchecked" ?>>
									<label for="office"> office documents</label>
							</td><td align="center">
								&nbsp; <font size="-1"><a href="search.php?advanced">Advanced</a></font> &nbsp;
						</td></tr>
					</table>
					</form>
				</td><td align="right" valign="top">
					<a href="search.php"><img src="wumpus_logo.gif" border="0"></a> <br>
					<font size="-2">Copyright &copy; 2005 by
					<a href="http://stefan.buettcher.org/" target="_blank">Stefan B&uuml;ttcher</a> &nbsp; &nbsp;</font>
				</td></tr></table>
				<br>
				<table width="100%" border="0" cellpadding="0" cellspacing="0">
					<tr><td bgcolor="#4070D0"><img width="1" height="1" alt=""></td></tr>
				</table>
				<table width="100%" border="0" cellpadding="0" cellspacing="0" bgcolor="#e5ecf9">
					<tr><td bgcolor="#E0F0FF" nowrap>
						<font size=+1>&nbsp;<?php print $title; ?></font>&nbsp;
					</td><td bgcolor="#E0F0FF" align="right" nowrap>
						<font size="-1"><?php print $content; ?></font>&nbsp;
					</td></tr>
				</table>
			<?php
		}
	} // end of printHeader()

	function printTrailer() {
		?>
		<br><br>
		</body></html>
		<?php
	} // end of printTrailer()

	function printIndexContents() {
		global $host, $port, $userID, $query, $username, $wumpus, $typeToDescription;
		$summary = $wumpus->getQueryResults("@summary");
		$indexArray = explode("\n", $summary);
		$masterInfo = $indexArray[0];
		$fsStr = strtok($masterInfo, "\t");
		$fileStr = strtok("\t");
		$dirStr = strtok("\t");
		$masterInfo = "Index covers $fsStr, containing $fileStr and $dirStr.";
		printHeader("<b>Contents of Local Index</b>", $masterInfo);
		print "<br> &nbsp; The index covers <b>$fsStr</b>:\n";
		print "<br><ul>\n";
		$i = 1;
		while ($indexArray[$i][0] != '@') {
			if (!$indexArray)
				break;
			$subInfo = $indexArray[$i];
			$fsStr = strtok($subInfo, "\t");
			$fileStr = strtok("\t");
			$dirStr = strtok("\t");
			print "<li><b>$fileStr</b> ($dirStr) indexed for file system rooted at <b>$fsStr</b>";
			print "<font size=\"-2\"><br><br></font>\n";
			$i++;
		}
		print "</ul>\n";
			
		print "&nbsp; File type statistics (all files searchable by user <b>$username</b>):\n";
		print "<ul>\n";
		$filestats = $wumpus->getQueryResults("@filestats");
		$fsarray = explode("\n", $filestats);
		$found = false;
		foreach ($fsarray as $ft) {
			$ft = trim($ft);
			if ($ft == "")
				continue;
			if ($ft[0] == "@")
				break;
			$found = true;
			$temp = explode(" ", $ft);
			$typeString = substr($temp[0], 0, strlen($temp[0]) - 1);
			$count = $temp[1];
			if (isset($typeToDescription[$typeString]))
				$desc = $typeToDescription[$typeString];
			else
				$desc = "Unknown files";
			if ((!strstr($desc, "documents")) && (!strstr($desc, "files")) && (!strstr($desc, "messages")))
				$desc .= " documents";
			if ($desc == "E-mail messages")
				$desc = "E-mail message (mbox) files";
			if ($count == 1)
				$desc = substr($desc, 0, strlen($desc) - 1);
			print "<li><b>$count $desc";
			print "</b> ($typeString) <font size=\"-2\"><br><br></font>\n";
		}
		if (!$found)
			print "<li>No searchable files in index. <br><br>\n";
		print "</ul>\n";
		printTrailer();
	} // end of printIndexContents()

	
	function createQueryVector($query) {
		$resultLen = 0;
		$result;
		$withinQuotes = false;
		$queryArray = explode("\"", $query);
		foreach ($queryArray as $queryElement) {
			$queryElement = trim($queryElement);
			if (strlen($queryElement) > 0) {
				if ($withinQuotes)
					$result[$resultLen++] = $queryElement;
				else {
					$subResult = explode(" ", $queryElement);
					foreach ($subResult as $subResultElement)
						$result[$resultLen++] = trim($subResultElement);
				}
			}
			$withinQuotes = !$withinQuotes;
		}
		return $result;
	} // end of createQueryVector(...)


	function extractXMLComponent($string, $tag) {
		$thing = strstr($string, "<$tag>");
		if (!$thing)
			return "";
		$thing = trim(substr($thing, strlen("<$tag>")));
		$otherthing = strstr($thing, "</$tag>");
		if (!$otherthing)
			return "";
		$result = substr($thing, 0, strlen($thing) - strlen($otherthing));
		return $result;
	} // end of extractXMLComponent(...)


	function qp_decode($s) {
		$s = $s . " ";
		$result = "";
		while (strlen($s) > 0) {
			$critical = strstr($s, "=?ISO-");
			if (!$critical)
				$critical = strstr($s, "=?iso-");
			if (!$critical) {
				$result .= $s;
				$s = "";
			}
			else {
				$result .= substr($s, 0, strlen($s) - strlen($critical));
				$end = strstr(substr($critical, 17), "?=");
				if (strlen($end) > 0) {
					$s = substr($end, 3);
					$start = substr($critical, 0, strlen($critical) - strlen($s));
					$start = substr($start, 0, strlen($start) - 3);
					$start = quoted_printable_decode($start . " ");
					if (strstr($start, "?q?") != "")
						$start = substr(strstr($start, "?q?"), 3);
					else if (strstr($start, "?Q?") != "")
						$start = substr(strstr($start, "?Q?"), 3);
					$result .= $start;
				}
				else {
					$s = "";
					$result .= quoted_printable_decode($critical);
				}
			}
		}
		return trim($result);
	} // end of qp_decode(...)


	function isAlphaNum($c) {
		if (($c >= 'a') && ($c <= 'z'))
			return true;
		if (($c >= '0') && ($c <= '9'))
			return true;
		return false;
	} // end of isAlphaNum(...)

	
	function highlightSearchTerms($snippet, $termArray) {
		$snippetCopy = strtolower($snippet . " ");
		for ($i = strlen($snippet) - 1; $i >= 0; $i--)
			if (!isAlphaNum($snippetCopy[$i]))
				$snippetCopy[$i] = ' ';
		$i = 0;
		$result = "";
		while ($i < strlen($snippet)) {
			$oldI = $i;
			while ($snippetCopy[$i] == ' ')
				$i++;
			if ($i > $oldI)
				$result .= substr($snippet, $oldI, $i - $oldI);
			$found = false;
			foreach ($termArray as $term) {
				$term = strtolower($term) . " ";
				if (substr($snippetCopy, $i, strlen($term)) == $term) {
					$result .= "<span style=\"background-color:#FFFF00;font-weight:bold\">" .
						substr($snippet, $i, strlen($term) - 1) . "</span>";
					$i += strlen($term) - 1;
					$found = true;
					break;
				}
			}
			if (!$found) {
				$oldI = $i;
				while (($i < strlen($snippet)) && ($snippetCopy[$i] != ' '))
					$i++;
				if ($i > $oldI)
					$result .= substr($snippet, $oldI, $i - $oldI);
			}
		}
		return $result;
	} // end of highlightSearchTerms(...)


	function breakLongWords($text) {
		return $text;
	} // end of breakLongWords(...)


	function printResultElement($line) {
		global $queryVector, $query;
		$filename = extractXMLComponent($line, "filename");
		$author = htmlentities(extractXMLComponent($line, "author"));
		if ($author != "")
			$author = qp_decode($author);
		$score = extractXMLComponent($line, "score");
		$title = extractXMLComponent($line, "title");
		$title = str_replace(">", "&gt;", str_replace("<", "&lt;", $title));
		if ($title != "") {
			$title = qp_decode(highlightSearchTerms($title, $queryVector));
		}
		else {
			$title = $filename;
			while (strstr($title, "/"))
				$title = substr(strstr($title, "/"), 1);
			$title = highlightSearchTerms($title, $queryVector);
		}
		if (strlen($title) > 80)
			$title = substr($title, 0, 80) . " ...";
		$indexFrom = extractXMLComponent($line, "dstart");
		$indexTo = extractXMLComponent($line, "dend");
		$type = extractXMLComponent($line, "type");
		$page = extractXMLComponent($line, "page");
		$date = extractXMLComponent($line, "date");
		$filesize = extractXMLComponent($line, "filesize");
		$snippet = extractXMLComponent($line, "snippet");
		$snippet = str_replace(">", "&gt;", str_replace("<", "&lt;", $snippet));
		$snippet = breakLongWords(qp_decode(highlightSearchTerms($snippet, $queryVector)));

		print "<table cellspacing=\"2\" cellpadding=\"2\" border=\"0\"><tr><td>\n";
		print "<font color=\"#0000C0\"><a href=\"search.php?getfrom=$indexFrom" .
			"&getto=$indexTo&filename=$filename&query=$query&filetype=$type\">";
		print "<b>$title</b></a></font> ";
		if ($page > 1) {
			if (strstr($page, "-") == null)
				print "(page $page) ";
			else
				print "(pages $page) ";
		}
		printf("&nbsp; <font size=\"-1\" color=\"#808080\">Score: %.2f</font> <br>\n", $score);
		print "</td></tr>";
		if (($author != "") || ($date != "")) {
			print "<tr><td><font size=\"-1\">\n";
			$localString = "";
			if ($author != "") {
				if ($localString != "") $localString .= " &nbsp;-&nbsp; ";
				$localString .= "Author: $author";
			}
			if ($date != "") {
				if ($localString != "") $localString .= " &nbsp;-&nbsp; ";
				$localString .= "Date: $date";
			}
			print "&nbsp;&nbsp;&nbsp; $localString</font></td></tr>\n";
		}

		print "<tr><td><font size=\"-1\">" . $snippet . "</font></td></tr>";

		print "<tr><td><font size=\"-1\" color=\"#006000\">$filename &nbsp;-&nbsp; ";
		print "$type &nbsp;-&nbsp; $filesize</font></td></tr>";
		print "</table>\n";
	} // end of printResultElement(...)


	function processGetQuery() {
		global $host, $port, $userID, $query, $wumpus;
		if (!$wumpus->connection) {
			header("Location: login.php");
			exit(0);
		}
		else {
			$from = $_GET['getfrom'];
			$to = $_GET['getto'];
			if ((!$to) || ($to < $from))
				$to = $from;
			$filename = $_GET["filename"];
			printHeader("<b>Text Result</b>", $filename);
			$text = $wumpus->getQueryResults("@get $from $to");
			if (substr($wumpus->status, 0, 2) == "@1") {
				print "<br><br><br><br><br><center>\n";
				print "<font size=\"+3\" color=\"#C00000\"><b><i>Permission Denied</i></b></font>\n";
				print "</center>\n";
				printTrailer();
			}
			else {
				print "<br>\n";
				print "<table width=\"800\"><tr><td width=\"20\">&nbsp;</td><td width=\"770\">\n";
				$lines = explode("\n", $text);
				foreach ($lines as $line) {
					if (substr($line, 0, 1) == "@")
						break;
					$line = str_replace("  ", "&nbsp; ", htmlentities($line));
					print str_replace("\t", "&nbsp; &nbsp;", $line) . "<br>\n";
				}
				print "</td></tr></table><br>\n";
				printTrailer();
			}
		}
	} // end of processGetQuery()


	function processQuery() {
		global $host, $port, $userID, $query, $wumpus;
		global $start, $end, $fileType, $queryVector, $typeDescription;
		$query = str_replace("\\", "", $_GET["query"]);
		if (!$wumpus->connection) {
			header("Location: login.php");
			exit(0);
		}
		else {
			$wumpusQuery = "@desktop[start=$start][end=$end]";
			if (isset($_GET["filename"])) {
				$filename = str_replace("&", "", str_replace("]", "", str_replace("[", "", $_GET["filename"])));
				$wumpusQuery .= "[filename=$filename]";
			}
			if (in_array("files", $fileType))
				$wumpusQuery .= " \"<file!>\"..\"</file!>\" by";
			else if (!in_array("everything", $fileType)) {
				foreach ($fileType as $ft)
					$wumpusQuery .= "[filetype=$ft]";
			}
			$queryVector = createQueryVector($query);
			if (count($queryVector) == 0)
				printIndexContents();
			else {
				for ($i = 0; $i < count($queryVector); $i++) {
					if ($i == 0)
						$wumpusQuery .= " \"$queryVector[$i]\"";
					else
						$wumpusQuery .= ", \"$queryVector[$i]\"";
				}
				$wumpusResponse = $wumpus->getQueryResults($wumpusQuery);

				$responseArray = explode("\n", $wumpusResponse);
				$responseLines = 0;
				while ((strlen($responseArray[$responseLines]) > 0) &&
				       ($responseArray[$responseLines][0] != '@'))
					$responseLines++;
				$timeElapsed = substr(strstr($wumpus->status, "("), 1);
				$milliSeconds = explode(" ", $timeElapsed);
				$milliSeconds = sprintf("%.3f", $milliSeconds[0] / 1000.0);

				if ($responseLines == 0) {
					printHeader("<b>Search Results</b> <font size=\"+0\">($typeDescription)</font>",
							"No matching documents found ($milliSeconds seconds).");
					?>
					<br>
					<table cellpadding="2" cellspacing="2" border="0" width="790">
						<tr><td>
							Wumpus was unable to find any documents that match your query. Are you sure
							that what you are looking for exists on this machine? You might also want
							to check your spelling.
						</td></tr>
					</table>
					<?php
					printTrailer();
				}
				else {
					if ($end > $responseLines)
						$end = $responseLines;
					$summary = sprintf("Results %d - %d of %d%s ($milliSeconds seconds)",
							$start + 1, $end, $responseLines, ($responseLines < 2000 ? "" : "+"));
					printHeader("<b>Search Results</b> <font size=\"+0\">($typeDescription)</font>", $summary);
					print "<table border=\"0\" width=\"760\"><tr><td width=\"10\">&nbsp;</td>\n";
					print "<td><table cellpadding=\"2\" cellspacing=\"2\" border=\"0\">\n";
					for ($i = $start; $i < $end; $i++) {
						print "<tr><td><font size=\"-2\"><br></font>";
						printResultElement($responseArray[$i]);
						print "</td></tr>";
					}
					print "</table></td></tr></table>\n";

					if ($responseLines > 10) {
						$q = str_replace(" ", "+", $query);
						$q = str_replace("\"", "%22", $q);
						print "<br><br><center>";
						print "<table cellspacing=\"2\" cellpadding=\"2\" border=\"0\"><tr><td align=\"center\">";
						print "Result page:</td></tr><tr><td>\n";
						$currentPage = (int)($start / 10) + 1;
						$countFrom = $currentPage - 4;
						if ($countFrom <= 1)
							$countFrom = 1;
						$countTo = $countFrom + 8;
						$maxPage = (int)(($responseLines + 9) / 10);
						if ($countTo > $maxPage)
							$countTo = $maxPage;
						$urlTypeString = "";
						foreach ($fileType as $ft)
							$urlTypeString .= "&filetype[]=$ft";
						if (isset($_GET["advanced"]))
							$urlTypeString .= "&advanced";
						if (isset($_GET["filename"]))
							$urlTypeString .= "&filename=" . str_replace("&", "", $_GET["filename"]);
						if ($currentPage > 1) {
							$pageBack = (int)(($start - 9) / 10) * 10;
							print "<a href=\"search.php?query=$q$urlTypeString";
							print "&start=$pageBack&end=" . ($pageBack + 10) . "\">&lt;&lt;</a> &nbsp;\n";
						}
						for ($i = $countFrom; $i <= $countTo; $i++) {
							if ($i == $currentPage)
								print "<font color=\"#A00000\"><b>$i</b></font> &nbsp;\n";
							else {
								print "<a href=\"search.php?query=$q$urlTypeString&start=" . (($i - 1) * 10) .
											"&end=" . ($i * 10) . "\">$i</a> &nbsp;\n";
							}
						}
						if ($currentPage < $maxPage) {
							$pageForward = $currentPage * 10;
							print "<a href=\"search.php?query=$q$urlTypeString&start=$pageForward" .
								"&end=" . ($pageForward + 10) . "\">&gt;&gt;</a> &nbsp;\n";
						}
						print "</td></tr></table>\n";
						print "</center>\n";
					}

					printTrailer();
				}

			} // end else [count($queryVector) > 0]

		}
	} // end of processQuery()


?>


