#!/usr/bin/perl -w
=copyright
   http.pl - an http server for the Wumpus File System Search front-end

   Copyright (C) 2005 Kevin Fong. All rights reserved.
   This is free software with ABSOLUTELY NO WARRANTY.
  
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
  
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
  
   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA
=cut

=description
   The program reads configuration data from 'http.rc' and from the command line.
   Command line parameters take precedence over configuration file parameters.

   The program uses the following command line parameters:
	port: port number on which to bind (integer)
	httproot: path to the root of the web server (string)
	allow: IP address with wildcards allowed to connect (string)

	authenticate: direct front-end to authenticate against indexing server (no value)
	noauthenticate: direct front-end not to authenticate against indexing server (no value)
	wumpusCFG: path to the wumpus configuration file (string)
	searchQuery: GCL query used in Wumpus @document queries (string)

	crawler: direct front-end to behave in web search mode (no value)
	nocrawler: direct front-end to behave in file system search mode (no value)
	crawlDirectory: path to the local copy of the web search data (string)

   See README for configuration details.
=cut

use strict;
use warnings;
use Config::General;
use HTTP::Daemon;
use HTTP::Response;
use Getopt::Long;
use POSIX qw(strftime);

# ignore the children exiting
$SIG{CHLD} = 'IGNORE';

# flush buffer on write
$| = 1;

# clear the environment variables
foreach my $key (keys %ENV) {
	if ($key ne 'LANG') {
		delete $ENV{$key};
	}
}

# read from the configuration file
my $rcfile = "http.rc";
my $redirect = 'index.pl';
my %config;

if (-e $rcfile && -r $rcfile) {
	my $configfile = Config::General->new($rcfile);
	%config = $configfile->getall();
} else {
	%config = (	"LocalPort" => 8080,
			"HTTPRoot" => 'www/');
	print "Unable to find configuration file 'http.rc'\n";
}
$config{'TimeOut'} = 60;
$config{'ReuseAddr'} = 1;

my $allow = $config{'Allow'};
unless (defined @$allow) {
	$config{'Allow'} = [$config{'Allow'}];
}
undef $allow;

# read from the command line using Getopt::Long
GetOptions (
	'port=i' => \$config{'LocalPort'},
	'authenticate!' => \$config{'Authenticate'},
	'crawler!' => \$config{'WebCrawler'},
	'wumpusCFG=s' => \$config{'WumpusCFG'},
	'crawlDirectory=s' => \$config{'CrawlDir'},
	'httproot=s' => \$config{'HTTPRoot'},
	'searchquery=s' => \$config{'SearchQuery'},
	'allow=s' => \@{$config{'Allow'}}
);

if (defined $config{'Authenticate'}) {
	if ($config{'Authenticate'} eq '0') {
		undef $config{'Authenticate'};
	}
}

if (defined $config{'WebCrawler'}) {
	if ($config{'WebCrawler'} eq '0') {
		undef $config{'WebCrawler'};
	}
}

# Wumpus front-end specific environment variables
# defined a encoding string for Crypt::Lite
$ENV{CL_HASH} = int (rand((2**32)-1));

# export values from rcfile
$ENV{SEARCH_QUERY} = $config{'SearchQuery'} if defined($config{'SearchQuery'});
$ENV{WUMPUS_AUTH} = $config{'Authenticate'} if defined($config{'Authenticate'});
$ENV{CRAWLER} = $config{'WebCrawler'} if defined($config{'WebCrawler'});
$ENV{CRAWL_DIR} = $config{'CrawlDir'} if defined($config{'CrawlDir'});

# grab values from Wumpus configuration file
$ENV{WUMPUS_PORT} = &wumpusPort if (defined $config{'WumpusCFG'});

# open log file
open (LOGFILE, ">>access.log");
autoflush LOGFILE 1;

# change the directory to HTTPRoot
chdir $config{'HTTPRoot'};

# start up the httpd
my $httpd = HTTP::Daemon->new( LocalPort => $config{'LocalPort'}, Timeout => $config{'Timeout'}, ReuseAddr => $config{'ReuseAddr'}) || die "Cannot create socket: $!\n";

my ($connection, $request);

# accept connections
while ($connection = $httpd->accept()) {
	# fork a child to handle request
	my $pid;
	if ($pid = fork) {
		next;
	} else {
		if (checkIP($connection)) {
			my $childPID;
			if ($childPID = fork) {
				local $SIG{TERM} = sub { kill('TERM', -$childPID); exit; };
				local $SIG{INT} = sub { kill('INT', -$childPID); exit; };
				my $nothing;
				my $value;
				# wait until connection closes
				while ($value = recv($connection, $nothing, 16, MSG_PEEK)) {
					sleep 1;
				}
				kill('TERM', -$childPID);
			} else {
				setpgrp(0,0);
				while ($request = $connection->get_request()) {
					if ($request->method eq 'GET' || $request->method eq 'POST') {
						print LOGFILE process_GET_POST($connection, $request);
					} else {
						print LOGFILE process_not_implemented($connection, $request);
					}
				}
			}
		} else {
			print LOGFILE join (' ', $connection->peerhost(), &currentTime, "Connection refused"), "\n";
		}
		shutdown($connection, 2);
		$connection->close();
		exit;
	}
} continue {
	# close the connection
	$connection->close();
}
close (LOGFILE);

sub process_not_implemented {
	my ($connection, $request) = @_;
	my $response = HTTP::Response->new(501);

	$connection->send_response($response);

	return join (' ', $connection->peerhost(), &currentTime, $request->method(), $request->uri, "501"), "\n";
}

sub process_GET_POST {
	my ($connection, $request) = @_;
	my ($response, $code);

	# split URI into components & query
	my ($uri_, $query) = split '\?', $request->uri;
	my $file_requested = substr($uri_,1);

	# special get -- returning file requests
	unless (-e $file_requested) {
		my @path = (split ('/', $file_requested));
		my $trunc = join ('/', @path[0...$#path-1]);
		if (-e $trunc && !(-d $trunc)) {
			$file_requested = $trunc;
		}
	}
	
	# process the request
	if (!(-d $file_requested) && -e $file_requested && -r $file_requested) {
		my $mode;
		$mode = (stat($file_requested))[2];
		
		if (-x $file_requested && ($mode & 00001)) {
			my $content = $request->content;

			my $executable_output;
			# pass information to interpreters via environment variables

			$ENV{CONTENT_LENGTH} = $request->header('Content-Length') if defined $request->header('Content-Length');
			$ENV{CONTENT_TYPE} = $request->header('Content-Type') if defined $request->header('Content-Type');
			$ENV{HTTP_COOKIE} = $request->header('Cookie') if defined $request->header('Cookie');
			$ENV{SERVER_ADDR} = $connection->sockhost();
			$ENV{REMOTE_ADDR} = $connection->peerhost();
			$ENV{SERVER_PROTOCOL} = 'HTTP/1.1';
			$ENV{GATEWAY_INTERFACE} = 'CGI/1.1';
			$ENV{REQUEST_METHOD} = $request->method;
			$ENV{REDIRECT_STATUS} = 'CGI';
			$ENV{SCRIPT_FILENAME} = $file_requested;
			$ENV{QUERY_STRING} = "";
			$ENV{QUERY_STRING} = $query if defined $query;

			if ($request->method eq 'GET') {
				$executable_output = `$file_requested`;
			} elsif ($request->method eq 'POST') {
				$ENV{REQUEST_METHOD} = 'GET';
				$ENV{QUERY_STRING} = $content if defined $content;
				$executable_output = `$file_requested`;
			}
			my @filelines = split /^/, $executable_output;

			$response = HTTP::Response->new(200);

			# grab the header lines from the output
			# CGI and PHP use  as linebreaks
			my $i = 0;
			my ($text, $field);

			# read from output until we get a newline
			while (defined ($filelines[$i]) && !($filelines[$i] =~ /^$/ || $filelines[$i] =~ /^$/)) {
				$filelines[$i++] =~ /(.*?):(.*?\n)/;
				$text = $1;
				$field = $2;
				if ($text eq 'Status') {
					$response->code($field);
				} else {
					$response->push_header($text => $field);
				}
			}
			$i++;
			$response->push_header('Cache-Control' => 'no-store');

			# use the remaining lines as the content
			$response->content(join '', @filelines[$i..$#filelines]);

			# send response to the user
			$connection->send_response($response);
			$code = 200;
		} elsif ($mode & 00004) {
			# send the file
			$connection->send_file_response($file_requested);
			$code = 200;
		} else {
			# insufficient permissions
			$connection->send_error(403);
			$code = 403;
		}
	} elsif (-d $file_requested) {
		$file_requested .= "/$redirect";
		$file_requested =~ s(//){/};
		$code = 302;
		$connection->send_redirect($file_requested, $code, '');
	} elsif ($file_requested eq '') {
		$file_requested = $redirect;
		$code = 302;
		$connection->send_redirect($file_requested, $code, '');
	} else {
		$connection->send_error(404);
		$code = 404;
	}

	# log request
	return join (' ', $connection->peerhost(), &currentTime, $request->method(), $request->uri, $code), "\n";
}

sub checkIP {
	my ($connection) = @_;
	my $peer = $connection->peerhost();

	my $allowed = $config{'Allow'};

	if (defined (@$allowed)) {
		foreach my $i (@$allowed) {
			return 1 if defined match($i, $peer);
		}
	} else {
		return 1 if defined match($allowed, $peer);
	}
	return 0;
}

sub match {
	my ($ip, $peer) = @_;
	my @addr = $ip =~ /^(\d*|\*)\.(\d*|\*)\.(\d*|\*)\.(\d*|\*)(\/.*)?$/;
	my @peeraddr = split /\./, $peer;
	my @netmask;

	if ($#addr== -1) {
		# no parse
		return undef;
	} elsif ($#addr == 4) {
		pop @addr;
	}

	for (my $j = 0; $j < 4; $j = $j + 1) {
		if ($addr[$j] eq '*') {
			$netmask[$j] = 0;
			$addr[$j] = 0;
		} elsif ($addr[$j] < 0 || $addr[$j] > 255) {
			return undef;
		} else {
			$netmask[$j] = 255;
		}
		return undef if ((($addr[$j] ^ $peeraddr[$j]) & $netmask[$j]) != 0);
	}
	return 1;
}

sub wumpusPort {
	if (defined $config{'WumpusCFG'}) {
		my $port;
		my $cfg = $config{'WumpusCFG'};
		if (defined (@$cfg)) {
			# more than one directory defined -- use the last one defined
			$cfg = @$cfg[-1];
		}

		if (open (CFG, $cfg)) {
			while (<CFG>) {
				if (/^TCP_PORT/) {
					$port = (split('=', $_))[1] + 0;
				}
			}
			close CFG;
		}
		if (defined $port) {
			return $port;
		} else {
			print "Cannot extract TCP_PORT from $config{'WumpusCFG'}.  Setting port to default.\n";
			return 1234;
		}
	} else {
		return undef;
	}
}

sub currentTime {
	return strftime "[%e %b %Y %H:%M:%S %z]" , localtime;
}
