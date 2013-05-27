#!/usr/bin/perl -w

use warnings;
use strict;

use CGI qw/:standard/;

print header(
	-location=>'wumpus/login.pl',
	-status=>302
);
