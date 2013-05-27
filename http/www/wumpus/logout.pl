#!/usr/bin/perl -w

use warnings;
use strict;

use CGI qw/:standard/;

my $ssid = cookie(-name=>"ssid", -value=>''); #, -expires=>'now');

print header(
	-cookie=>$ssid,
	-location=>'login.pl',
	-status=>302
);
