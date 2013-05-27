<?php


	class WumpusConnection {

		// hostname of the index server
		var $host;

		// portnumber index server is listening on
		var $port;

		// socket connection to server
		var $connection;

		var $username;

		var $password;

		// index server status line
		var $status;

		function WumpusConnection($host, $port, $username, $password) {
			$this->host = $host;
			$this->port = $port;
			$this->userName = $username;
			$this->password = $password;
			$this->connection = @fsockopen($host, $port, $errno, $errstr, 2);
			if ($this->connection) {
				if (feof($this->connection)) {
					$this->status = "@1-Not connected.";
					fclose($this->connection);
					$this->connection = null;
				}
				$response = fgets($this->connection);
				if ((!$this->connection) || (substr($response, 0, 2) == "@1")) {
					$this->status = $response;
					fclose($this->connection);
					$this->connection = null;
				}
				else {
					fwrite($this->connection, "@login $username $password\n");
					fflush($this->connection);
					$this->status = fgets($this->connection);
				}
			}
			else {
				$this->status = "@1-Not connected.";
			}
		} // end of WumpusConnection(...)


		function getQueryResults($queryString) {
			if (!$this->connection) {
				$this->status = "@1-Not connected.";
				return $this->status;
			}
			fwrite($this->connection, $queryString . "\n");
			fflush($this->connection);
			$result = "";
			while (!feof($this->connection)) {
				$line = fgets($this->connection);
				if (strlen($line) > 0) {
					$result .= $line;
					if ($line[0] == '@') {
						$this->status = $line;
						break;
					}
				}
			}
			return $result;
		} // end of getQueryResults(...)


	} // end of class WumpusConnection


?>
