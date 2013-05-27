#!/usr/bin/perl

use warnings;
use strict;

use CGI qw/-no_xhtml :standard *table *center center/;
use WumpusConnection;
use Crypt::Lite;

my $crypt = Crypt::Lite->new();

my $pageTitle = "Wumpus File System Search";
my $pageColor = "#FFFFFF";
my $pageStyle = <<END;
body,td,div,.p,a {
	font-family:arial,
	sans-serif
}
END
my ($infobar_Title, $infobar_Content);

my $page_body;

my $username = '';
my $password = '';

$username = cookie('username') if defined (cookie('username'));
$password = cookie('password') if defined (cookie('password'));

$username = param('username') if defined (param('username'));
$password = param('password') if defined (param('password'));

if (defined (cookie('ssid'))) {
	my $up = $crypt->decrypt(cookie('ssid'), $ENV{CL_HASH});
	if ($up ne '') {
		($username, $password) = split (':', $up);
	}
}

if (defined $username && defined $password && $username ne '' && $password ne '') {
	# check username/password combination
	my $connection = WumpusConnection->new();
	if ($connection->{connected} == 1) {
		if ($connection->wumpus_connect($username, $password) == 1) {
			my $cookie = cookie(-name=>"ssid", -value=>$crypt->encrypt("$username:$password", $ENV{CL_HASH}));
			my $usercookie = cookie(-name=>"username", -value=>'', -expires=>'now');
			my $passcookie = cookie(-name=>"password", -value=>'', -expires=>'now');

			print header(
				-cookie=>[$cookie, $usercookie, $passcookie],
				-location=>'search.pl',
				-status=>'302'
			);
			exit;
		} else {
			my $cookie = cookie(-name=>"ssid", -value=>'', -expires=>'now');
			my $usercookie = cookie(-name=>"username", -value=>'', -expires=>'now');
			my $passcookie = cookie(-name=>"password", -value=>'', -expires=>'now');
			print header(
				-cookie=>[$cookie, $usercookie, $passcookie]
			);
			$infobar_Title = "&nbsp; Authentication Failed";
			$page_body = <<END;
				<center><font size=\"+2\" color=\"#C00000\"><b>Authentication failed</b></font></center><br>
				The Username/Password combination you provided was not found in the user
				database of the indexing service. Please check the spelling and try again.
END
		}
	} else {
		print header();
		$infobar_Title = "&nbsp; Not Connected";
		$page_body = "Unable to connect to index server. Make sure the indexing service is running.\n";
	}
} else {
	my $cookie = cookie(-name=>"ssid", -value=>'', -expires=>'now');
	print header( -cookie=>[$cookie], -charset=>'UTF-8', -content_type=>'text/html', -expires=>'-1d');
	$infobar_Title .= "&nbsp; Authentication Required";
	$page_body = <<END;
		Before you can use this search interface, the Wumpus wants to know who you
		are. Convince him that you are allowed to search this computer by entering
		your username and password into the form below. Please note that this can
		either be your ordinary Linux account password, or a special Wumpus search
		password provided by your system administrator.
END
}

# generate login page
my $start_html = start_html(
	-title=>$pageTitle,
	-style=>{-code=>$pageStyle},
	-BGCOLOR=>$pageColor);
$start_html =~ s/<!DOCTYPE.*?>//s;

print $start_html;
print "\n";

print center( img({-src=>"wumpus_logo_big.gif"}) );
print br;

print "\n";
print comment ('info bar'), "\n";
print body_InformationBar();
print br;
print "\n";

print comment ('form'), "\n";
print $page_body;
print br;
print br;
print "\n";

print start_center();
print start_form(-method=>'POST', -action=>url(absolute=>1));
print start_table();

print Tr(
	td([
		"Username:",
		textfield(
			-name=>'username',
			-value=>'',
			-size=>'32',
			-maxlength=>'32'
		)
	])
);

print Tr(
	td([
		"Password:", 
		password_field(
			-name=>'password',
			-value=>'',
			-size=>'32',
			-maxlength=>'32'
		)
	])
);
print end_table();
print submit(-name=>'Authenticate');
print end_form();
print end_center();
print end_html();

# Information Bar at the top of the page
sub body_InformationBar {
	my $return;

	$return .= table({-width=>"100%", -border=>"0", -cellpadding=>"0", -cellspacing=>"0"},
		Tr([
			td(
				{-BGCOLOR=>"#4070D0"}, img({-width=>"1", -height=>"1", -alt=>""})
			)
		])
	);

	$return .= table({-width=>"100%", -BGCOLOR=>"#E5ECF9", -border=>"0", -cellpadding=>"0", -cellspacing=>"0"},
		Tr(
			td(
				{-BGCOLOR=>"#E0F0FF", -nowrap=>"true"}, b($infobar_Title)
			),
			td(
				{-BGCOLOR=>"#E0F0FF", -align=>"right", -nowrap=>"true"}, [font({-size=>"-1"}, $infobar_Content)]
			)
		)
	);
	return $return;
}
