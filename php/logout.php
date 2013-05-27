<?php
	header("Cache-Control: no-cache");
	header("Pragma: no-cache");
	setcookie("username", "");
	setcookie("password", "");
	header("Location: login.php");
	exit(0);
?>
