<?php

	include_once("wumpus.php");
	include_once("configuration.php");

	header("Cache-Control: no-cache");
	header("Pragma: no-cache");

	$username = "";
	$password = "";

	if ((isset($_GET["username"])) && (isset($_GET["password"]))) {
		$username = $_GET["username"];
		$password = $_GET["password"];
	}
	else if ((isset($_POST["username"])) && (isset($_POST["password"]))) {
		$username = $_POST["username"];
		$password = $_POST["password"];
	}
	else if ((isset($_COOKIE["username"])) && (isset($_COOKIE["password"]))) {
		$username = $_COOKIE["username"];
		$password = $_COOKIE["password"];
	}

	if ($username != "") {
		$wumpus = new WumpusConnection($host, $port, $username, $password);
		if (substr($wumpus->status, 0, 2) == "@1") {
			setcookie("username", "");
			setcookie("password", "");
		}
		else {
			setcookie("username", $username);
			setcookie("password", $password);
			header("Location: search.php");
			exit(0);
		}
	}

?>

<html>
<head>
	<title>Wumpus File System Search - Login</title>
	<style>
		<!-- body,td,div,.p,a{font-family:arial,sans-serif } //-->
	</style>
</head>
<body bgcolor="#FFFFFF">
	<center>
		<img src="wumpus_logo_big.gif">
	</center>
	<br>
	<table width="100%" border="0" cellpadding="0" cellspacing="0">
		<tr><td bgcolor="#4070D0"><img width="1" height="1" alt=""></td></tr>
	</table>
	<table width="100%" border="0" cellpadding="0" cellspacing="0" bgcolor="#e5ecf9">
		<tr><td bgcolor="#E0F0FF" nowrap>
			<font size=+1>&nbsp;<b><?php
				if ($username == "")
					print "Authentication Required";
				else if ($wumpus->status == "@1-Not connected.")
					print "Not Connected";
				else
					print "Authentication Failed";
			?></b></font>&nbsp;</td><td bgcolor="#E0F0FF" align="right" nowrap>
			<font size="-1">&nbsp;</font>&nbsp;
		</td></tr>
	</table>
	<br>
	<table cellpadding="2" cellspacing="2" border="0" width="100%">
		<tr><td>
<?php
	if ($username == "") {
		print "Before you can use this search interface, the Wumpus wants to know who you\n";
		print "are. Convince him that you are allowed to search this computer by entering\n";
		print "your username and password into the form below. Please note that this can\n";
		print "either be your ordinary Linux account password, or a special Wumpus search\n";
		print "password provided by your system administrator.\n";
	}
	else {
		if ($wumpus->status == "@1-Not connected.") {
			print "Unable to connect to index server. Make sure the indexing service is running.\n";
			print "Make also sure the host/port configuration of the PHP scripts is compatible\n";
			print "with the main configuration file of the indexing service.\n";
		}
		else {
			print "<center><font size=\"+2\" color=\"#C00000\"><b>Authentication failed</b></font></center><br>\n";
			print "The Username/Password combination you provided was not found in the user\n";
			print "database of the indexing service. Please check the spelling and try again.\n";
		}
	}
?>
		</td></tr>
	</table>
	<br><br>
	<center>
		<form action="login.php" method="POST">
			<table border="2" bgcolor="#E0E0E0">
				<tr><td><table cellspacing="2" cellpadding="2" border="0">
					<tr><td>
						Username:
					</td><td>
						<input type="text" name="username" size="32" maxlength="32" value="">
					</td></tr>
					<tr><td>
						Password:
					</td><td>
						<input type="password" name="password" size="32" maxlength="32" value="">
					</td></tr>
				</table></td>
			</tr></table>
			<br>
			<input type="submit" value="Authenticate">
		</form>
	</center>
</body>
</html>

