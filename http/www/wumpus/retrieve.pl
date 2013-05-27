#!/usr/bin/perl
use strict;
# use warnings;

use WumpusConnection;
use CGI qw/-no_xhtml -oldstyle_urls :standard *ul *il *table/;
use POSIX;
use Crypt::Lite;
$| = 1;

################################################################################
# connecting to the indexing server
################################################################################
my $crypt = Crypt::Lite->new();

# establish connection with indexing server
my $connection = WumpusConnection->new();

# verify that connection exists
if ($connection->{connected} == 0) {
#	print redirect('login.pl');
	exit;
}

# decrypt the username/password
my ($username, $password) = split(':', $crypt->decrypt(cookie('ssid'), $ENV{CL_HASH}));

# authenticate against the server
if (defined ($ENV{WUMPUS_AUTH})) {
	if ($connection->wumpus_connect($username, $password) != 1) {
#		print redirect('login.pl');
		exit;
	}
}

################################################################################

my ($filetype, $filename) = $connection->wumpus_fileinfo(param('idx'));
my ($file) = $connection->wumpus_getfile($filename);

print header(-content_type=>$filetype, -expires=>'-1d');
print $file;
exit;
