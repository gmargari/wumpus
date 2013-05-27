#!/usr/bin/perl
use strict;
use warnings;

use WumpusConnection;
use CGI qw/-no_xhtml -oldstyle_urls :standard *ul *il *table/;
use POSIX;
use Crypt::Lite;

my $crypt = Crypt::Lite->new();

$| = 1;

# attempt to establish a connection with the indexing server

my $connection = WumpusConnection->new();

if ($connection->{connected} == 0) {
	print redirect('login.pl');
	exit;
}

my ($username, $password) = split(':', $crypt->decrypt(cookie('ssid'), $ENV{CL_HASH}));

if ($connection->wumpus_connect($username, $password) != 1) {
	print redirect('login.pl');
	exit;
}

my $pageTitle = "Wumpus File System Search";
my $pageColor = "#FFFFFF";
my $pageStyle = <<END;
body,td,div,.p,a {
	font-family:arial,
	sans-serif
}
END

my $paramSearchField = param('searchfield') || '';
my $paramFileType = param('searchtype') || '';
my $paramFileName = param('filename') || '';

my $formMethod = 'GET';
my @fileType = ( "text/html", "application/pdf", "application/postscript", "text/plain", "application/msword", "text/xml", "text/x-mail", "audio/mpeg", "application/multitext");
my %fileTypeToDescription = (
	"text/html" => "HTML documents",
	"application/pdf" => "Adobe PDF",
	"application/postscript" => "Adobe PostScript",
	"text/plain" => "Text files",
	"application/msword" => "Microsoft Office",
	"text/xml" => "XML documents",
	"text/x-mail" => "E-mail messages",
	"audio/mpeg" => "MPEG audio files",
	"application/multitext" => "MultiText input files"
);

my %searchTypeToDescription = (
	"everything" => "Everything",
	"text/x-mail" => "E-mail messages",
	"office" => "Office documents",
	"files" => "Files"
);

my $advancedFlag = (defined param('advanced'));
my $tframe = (defined param('tframe'));
my $bframe = (defined param('bframe'));

Delete('advanced', 'tframe', 'bframe');

my $absURL= url(-absolute=>1);
my $absURL_Query= url(-absolute=>1,-query=>1);
param(-name=>'advanced', -value=>'');
my $absURL_AdvancedQuery= url(-absolute=>1,-query=>1);
Delete('advanced');

################################################################################
# ---------- HTML generation starts here ---------- 
################################################################################
{
	# Process Query
	my ($pageContent, $infobarTitle, $infobarContent, $frame, $contentType) = processQuery();

	if (!(defined $contentType)) {
		$contentType = 'text/html';
	}

	print header(-charset=>'UTF-8', -content_type=>$contentType);

	if ($frame || $bframe) {
		print $pageContent;
		exit;
	}

	print start_html(
		-title=>$pageTitle,
		-style=>{-code=>$pageStyle},
		-BGCOLOR=>$pageColor,
		-head=>base({-target=>'_top'}));
	print "\n";

	# print body header
	print comment ('search box'), "\n";
	if ($advancedFlag) {
		print bodyAdvancedSearchBox();
	} else {
		print bodySearchBox();
	}
	print "\n";

	# print information bar
	print comment ('info bar'), "\n";
	print bodyInformationBar($infobarTitle, $infobarContent);
	print "\n";

	# print content
	print comment ('content'), "\n";
	print $pageContent;
	print "\n";

	# print footer
	print end_html;
}

################################################################################
# ---------- HTML generation ends here ---------- 
################################################################################

################################################################################
# &sendSearchQuery
################################################################################
sub sendSearchQuery {
	my ($search_string) = @_;
	my ($result, $time) = $connection->wumpus_search($search_string, param('start') || 0, param('end') || 9);
	my @count = $connection->wumpus_extract($result, "count");
	my @index = $connection->wumpus_extract($result, "rank");
	my @results = $connection->wumpus_extract($result, "document");
	return ($time, $count[0], $index[0], @results);
}

################################################################################
# &formatSearchString
################################################################################
sub formatSearchString {
	my @tokens;
	foreach ($paramSearchField =~ /(?:"(.*?)")|(\S*)/g) {
		if (defined($_) && $_ ne '') {
			push @tokens, "\"$_\"";
		}
	}
	return join ', ', @tokens;
}

################################################################################
# &processResult
################################################################################
sub processResult {
	my ($result_string) = @_;

	my $title = \&titleGeneric;
	my $header = \&headerGeneric;
	my $content = \&contentGeneric;
	my $footer = \&footerGeneric;
	my $img = '';

	my $document_type = ($connection->wumpus_extract($result_string, "document_type"))[0];

	if ($document_type eq 'text/html') {
		$title = \&titleHTML;
		$content = \&contentHTML;
		$img = img({-src=>'/img/gnome-mime-text-html.png'});
	} elsif ($document_type eq 'text/x-mail') {
		$title = \&titleXMail;
		$header = \&headerXMail;
		$content = \&contentXMail;
		$img = img({-src=>'/img/emblem-mail.png'});
	} elsif ($document_type eq 'application/pdf') {
		$img = img({-src=>'/img/gnome-mime-application-pdf.png'});
	}

	return table({-cellspacing=>2, -cellpadding=>2, -width=>"50%"},
		Tr([
			# title
			td({-colspan=>'2'}, [
				font({-size=>"+1"}, a({-href=>&titleHREF}, b($title->($result_string))))
			]),
			# header & snippet
			td([
				$img, $header->($result_string) . $content->($result_string)
			]),
			# footer
			td({-colspan=>'2'}, [
				font({-size=>"-1"}, $footer->($result_string))
			])
		])
	);
}

################################################################################
# Functions used in &processResult
# For generic files 
################################################################################
sub titleHREF {
	my ($result_string) = @_;
	my $doc_start = ($connection->wumpus_extract($result_string, "document_start"))[0];
	my $doc_end = ($connection->wumpus_extract($result_string, "document_end"))[0];
	my $filename = ($connection->wumpus_extract($result_string, "filename"))[0];
	my $document_type = ($connection->wumpus_extract($result_string, "document_type"))[0];
	my $filesize = ($connection->wumpus_extract($result_string, "filesize"))[0];
	my $timeModified = ($connection->wumpus_extract($result_string, "modified"))[0];

	return "$absURL?searchfield=$paramSearchField&ds=$doc_start&de=$doc_end&fn=$filename&dt=$document_type&fs=$filesize&mod=$timeModified";
}

sub titleGeneric {
	my ($result_string) = @_;

	return ($connection->wumpus_extract($result_string, "filename"))[0];
}

sub headerGeneric {
	return '';
}

sub footerGeneric {
	my ($result_string) = @_;
	my $filename = ($connection->wumpus_extract($result_string, "filename"))[0];
	my $document_type = ($connection->wumpus_extract($result_string, "document_type"))[0];
	my $score = ($connection->wumpus_extract($result_string, "score"))[0];
	my $page = ($connection->wumpus_extract($result_string, "page"))[0];
	my $timeModified = ($connection->wumpus_extract($result_string, "modified"))[0];
	my $filesize = ($connection->wumpus_extract($result_string, "filesize"))[0];

	my $footer = "$filename - $document_type<br>Score $score";
	if (defined $page && $page != 1) {
		if ($page =~ /-/) {
			$footer .= " - Pages $page";
		} else {
			$footer .= " - Page $page";
		}
	}

	if (defined $timeModified) {
		$footer .= " - " . &timeToString($timeModified);
	}

	if (defined $filesize) {
		$footer .= " - " . &sizeToString($filesize);
	}

	$footer .= " " . a({-href=>&titleHREF."&source="}, "View Source");

	return $footer;
}

sub contentGeneric {
	my ($result_string) = @_;
	my $snippet = ($connection->wumpus_extract($result_string, "snippet"))[0];
	my $encoding = 'ISO-8859-1';

	if (defined($snippet)) {
		# decode the snippet
		$snippet = decodeToUTF($snippet, $encoding);

		# replace the <passage!> tags with #^passage^# to avoid being removed by HTML stripper
		$snippet =~ s/<passage!>/#^passage^#/g;
		$snippet =~ s/<\/passage!>/#^\/passage^#/g;

		$snippet = trimSnippet($snippet);
	} else {
		$snippet = '';
	}

	return $snippet;
}

################################################################################
# &trimSnippet
#
# Requires passage to be marked with #^passage^# and #^/passage^# tags
#
# Returns snippet of length at most $n_tokens
################################################################################
sub trimSnippet {
	my ($snippet) = @_;
	my $n_tokens = 30;

	if ($snippet =~ /#\^passage\^#/) {
		# passage still exists in text; extract a snippet of reasonable size surrounding the passage

		my @snippet_tokens = split ' ', $snippet;
		if ($#snippet_tokens > $n_tokens) {
			my (@before_pass, @pass);

			my $current_token = shift @snippet_tokens;
			while ($current_token !~ /#\^passage\^#/) {
				push @before_pass, $current_token;
				$current_token = shift @snippet_tokens;
			}

			while ($current_token !~ /#\^\/passage\^#/) {
				push @pass, $current_token;
				$current_token = shift @snippet_tokens;
			}
			push @pass, $current_token;

			my $tokens_needed = $n_tokens - $#pass;
			if ($tokens_needed / 2 > $#before_pass) {
				unshift @pass, @before_pass;
				while ($#pass < $n_tokens) {
					push @pass, (shift @snippet_tokens);
				}
				$snippet = (join ' ', @pass) . '...';
			} elsif ($tokens_needed / 2 > $#snippet_tokens) {
				push @pass, @snippet_tokens;
				while ($#pass < $n_tokens) {
					unshift @pass, (pop @before_pass);
				}
				$snippet = '...' . (join ' ', @pass);
			} else {
				while ($#pass < $n_tokens) {
					unshift @pass, (pop @before_pass);
					push @pass, (shift @snippet_tokens);
				}
				$snippet = '...' . (join ' ', @pass) . '...';
			}
			# change snippet
		}
		$snippet =~ s/#\^passage\^#//g;
		$snippet =~ s/#\^\/passage\^#//g;
	} else {
		# passage tag not in text
		$snippet = "";
	}

	return $snippet;
}

################################################################################
# Functions used in &processResult
# For HTML files 
################################################################################
sub titleHTML {
	my ($result_string) = @_;
	my $filename = ($connection->wumpus_extract($result_string, "filename"))[0];
	my $headers = ($connection->wumpus_extract($result_string, "headers"))[0];

	return ($connection->wumpus_extract_html($headers, "title"))[0] || $filename;
}

sub contentHTML {
	my ($result_string) = @_;
	my $snippet = ($connection->wumpus_extract($result_string, "snippet"))[0];
	my $headers = ($connection->wumpus_extract($result_string, "headers"))[0];
	my $encoding = 'ISO-8859-1';

	# extract the first character set from the META tag
	my @encoding = $headers =~ /<(?:meta|META).*?charset(.*?)>/;

	if (defined $encoding[0]) {
		@encoding = $encoding[0] =~ /([\w-]+)/;
		$encoding = $encoding[0];
	}

	if (defined($snippet)) {
		# decode the snippet
		$snippet = decodeToUTF($snippet, $encoding);

		# replace the <passage!> tags with #^passage^# to avoid being removed by HTML stripper
		$snippet =~ s/<passage!>/#^passage^#/g;
		$snippet =~ s/<\/passage!>/#^\/passage^#/g;

		# strip HTML
		$snippet = stripHTML($snippet);

		# trim snippet
		$snippet = trimSnippet($snippet);
	} else {
		$snippet = '';
	}

	return $snippet;
}

################################################################################
# Functions used in &processResult
# For text/x-mail files 
################################################################################
sub titleXMail {
	my ($result_string) = @_;
	my $filename = ($connection->wumpus_extract($result_string, "filename"))[0];
	my $headers = ($connection->wumpus_extract($result_string, "headers"))[0];
	$headers = decodeMailHeader($headers);
	
	my @subj = $headers =~ /^Subject: (.*)/m;

	return $subj[0] || $filename;
}

sub headerXMail {
	my ($result_string) = @_;
	my $headers = ($connection->wumpus_extract($result_string, "headers"))[0];
	$headers = decodeMailHeader($headers);

	my @to = $headers =~ /^To: (.*)/m;
	my @from = $headers =~ /^From: (.*)/m;
	my @date = $headers =~ /^Date: (.*)/m;

	my $header = '';

	if (defined($date[0])) {
		$header.= b("Date: ") . escapeHTML($date[0]) . br;
	}

	if (defined($from[0])) {
		$header.= b("From: ") . escapeHTML($from[0]) . br;
	}

	if (defined($to[0])) {
		$header.= b("To: ") . escapeHTML($to[0]) . br;
	}

	return $header;
}

sub contentXMail {
	my ($result_string) = @_;
	my $snippet = ($connection->wumpus_extract($result_string, "snippet"))[0];
	my $headers = ($connection->wumpus_extract($result_string, "headers"))[0];
	my $encoding = 'ISO-8859-1';

	# extract the character set from the Content-Type line
	my @encoding = $headers =~ /^Content-Type:.*?; charset\s*=\s*(.*?)(;.*)*$/m;

	# extract the context type from the Content-Type line
	my @contentType = $headers =~ /^Content-Type:\s*(.*?); charset\s*=\s*(.*?)(;.*)*$/m;

	if (defined $encoding[0]) {
		@encoding = $encoding[0] =~ /([\w-]+)/;
		$encoding = $encoding[0];
	}

	if (defined($snippet)) {
		# decode the snippet
		$snippet = decodeToUTF($snippet, $encoding);

		# replace the <passage!> tags with #^passage^# to avoid being removed by HTML stripper
		$snippet =~ s/<passage!>/#^passage^#/g;
		$snippet =~ s/<\/passage!>/#^\/passage^#/g;

		if (defined $contentType[0] && $contentType[0] =~ /text\/html/i) {
			# strip HTML
			$snippet = stripHTML($snippet);
		}

		# trim snippet
		$snippet = trimSnippet($snippet);
	} else {
		$snippet = '';
	}

	return $snippet;
}

################################################################################
# Generate links to other pages of results
################################################################################
sub generatePageSelection {
	my ($count, $index) = @_;
	my ($start, $end);
	my $return;
	my $additionalqueries;

	my $cp = POSIX::floor($index/10);
	my $low = $cp - 4;
	my $high = $cp + 5;

	my $np = POSIX::floor($count/10);

	if ($high > $np) {
		$low -= $high - $np;
		$high = $np;
	}

	if ($low < 0) {
		$high += -$low;
		$high = $np if $high > $np;
		$low = 0;
	}

	foreach (param('searchtype[]')) {
		$additionalqueries .= "&searchtype[]=$_";
	}

	if (defined param('searchtype')) {
		$additionalqueries .= "&searchtype=" . param('searchtype');
	}

	if (defined param('filename')) {
		$additionalqueries .= "&filename=" . param('filename');
	}

	if ($advancedFlag) {
		$additionalqueries .= "&advanced=";
	}

	my ($prevPage, $nextPage);

	for (my $i = $low; $i <= $high; $i++) {
		$start = ($i * 10);
		$end = (($i + 1) * 10)-1;

		# create a << link to previous page
		if ($cp != $low && $i == $cp-1) {
			$prevPage = a({-href=>"$absURL?start=$start&end=$end&searchfield=$paramSearchField$additionalqueries"}, "<<") . "&nbsp;";
		}

		# don't make a link to the current page
		if ($i != $cp) {
			$return .= a({-href=>"$absURL?start=$start&end=$end&searchfield=$paramSearchField$additionalqueries"}, $i+1);
		} else {
			$return .= $i+1;
		}

		# create a >> link to next page
		if ($cp != $high && $i == $cp+1) {
			$nextPage = "&nbsp;" . a({-href=>"$absURL?start=$start&end=$end&searchfield=$paramSearchField$additionalqueries"}, ">>");
		}
		$return .= "\n";
	}

	return $prevPage . $return . $nextPage;
}

################################################################################
# Generate search box at top of page
################################################################################
sub bodySearchBox {
	my $return;

	my $logo = img({-src=>"wumpus_logo.gif"});
	my $copyright = font({-size=>-1}, "Copyright &copy; 2005 by " . a({-href=>"http://stefan.buettcher.org/"},"Stefan B&uuml;ttcher"));
	my $query_submit_button = submit(-label=>"Submit");

	my $searchfield = textfield(
		-name=>'searchfield',
		-value=>$paramSearchField,
		-size=>56,
		-maxlength=>256
	);
	
	my $filetype = radio_group(
		-name=>'searchtype',
		-values=>['everything', 'files', 'text/x-mail', 'office'],
		-default=>'everything',
		-labels=>\%searchTypeToDescription
	);

	$return .= start_form(-method=>$formMethod, -action=>$absURL);
	$return .= table(
		td([
			table([
				td({-valign=>"top"}, $logo),
				td($copyright)
			]),
			table(
				Tr([
					td(["Search query:", $searchfield, $query_submit_button]),
					td({-align=>'center'}, [font({-size=>"-1"},a({-href=>"logout.pl"}, "Logout")), font({-size=>"-1"}, $filetype), font({-size=>"-1"}, a({-href=>"$absURL_AdvancedQuery"}, "Advanced"))])
				])
			)
		])
	);
	$return .= end_form;

	return $return;
}

################################################################################
# Generate advanced search box at top of page
################################################################################
sub bodyAdvancedSearchBox {
	my $return;

	my $logo = img({-src=>"wumpus_logo.gif"});
	my $copyright = font({-size=>-1}, "Copyright &copy; 2005 by " . a({-href=>"http://stefan.buettcher.org/"},"Stefan B&uuml;ttcher"));
	my $query_submit_button = submit(-label=>"Submit");

	my $searchfield = textfield(
		-name=>'searchfield',
		-value=>$paramSearchField,
		-size=>56,
		-maxlength=>256
	);

	my $fnfield = textfield(
		-name=>'filename',
		-value=>$paramFileName,
		-size=>40,
		-maxlength=>40
	);

	my $filetype = scrolling_list(
		-name=>'searchtype[]',
		-values=>[@fileType],
		-size=>7,
		-multiple=>'true',
		-labels=>\%fileTypeToDescription
	);

	$return .= start_form(-method=>$formMethod, -action=>$absURL);
	$return .= hidden(-name=>'advanced');
	$return .= table(
		td({-valign=>"top"}, [
			table([
				td($logo),
				td($copyright)
			]),
			table(
				Tr([
					td(["Search query:", $searchfield, $query_submit_button]),
					td({-valign=>'top'}, [
						font({-size=>"-1"}, a({-href=>"logout.pl"}, "Logout")),
						table([
							Tr({-valign=>'top'}, [
								td([
									font({-size=>"-1"}, $filetype),
									table([
										Tr([
											td([
												b("File name") . " (wild card expression)" . br . $fnfield
											])
										])
									])
								])
							])
						]),
						font({-size=>"-1"}, a({-href=>$absURL_Query}, "Standard"))
					])
				])
			)
		])
	);
	$return .= end_form;

	return $return;
}

################################################################################
# Generate Information Bar at the top of the page
################################################################################
sub bodyInformationBar {
	my ($infobarTitle, $infobarContent) = @_;
	my $return;

	$return .= table({-width=>"100%", -border=>"0", -cellpadding=>"0", -cellspacing=>"0"},
		Tr([
			td(
				{-BGCOLOR=>"#4070D0"}, [img({-width=>"1", -height=>"1", -alt=>""})]
			)
		])
	);

	$return .= table({-width=>"100%", -BGCOLOR=>"#E5ECF9", -border=>"0", -cellpadding=>"0", -cellspacing=>"0"},
		Tr(
			td(
				{-BGCOLOR=>"#E0F0FF", -nowrap=>'true'}, [font({-size=>"+1"}, $infobarTitle)]
			),
			td(
				{-BGCOLOR=>"#E0F0FF", -align=>"right", -nowrap=>'true'}, [font({-size=>"-1"}, $infobarContent)]
			)
		)
	);
	return $return;
}

################################################################################
# Process Query
################################################################################
sub processQuery {
	my $query;
	if (defined (param('fn')) && param('fn') ne '') {
		if ($tframe) {
			$query = \&queryGet;
		} elsif ($bframe) {
			$query = \&queryGet;
		} else {
			$query = \&queryGetFrame;
		}
	} elsif (defined (param('searchfield')) && param('searchfield') ne '') {
		$query = \&querySearch;
	} else {
		$query = \&queryStatistics;
	}
	return &$query;
}

################################################################################
# &queryGetFrame
# Get file contents from indexing server 
################################################################################
sub queryGetFrame {
	my ($url, $b_url, $ratio);
	if ($advancedFlag) {
		$url = $absURL_AdvancedQuery;
		$ratio = "25%, 75%";
	} else {
		$url = $absURL_Query;
		$ratio = "15%, 85%";
	}

	if (defined (param('source'))) {
		my $fn = (split ('/', param('fn')))[-1];
		$b_url = $url;
		$b_url =~ s/\?/\/$fn\?/;
	} else {
		$b_url = $url;
	}

	my $page = <<END;
<FRAMESET rows="$ratio">
	<FRAME src="$url&tframe=" frameborder=0>
	<FRAME src="$b_url&bframe=" frameborder=0>
</FRAMESET>
END
	return ($page, '', '', 1);
}


################################################################################
# &queryGet
# Get file contents from indexing server 
################################################################################
sub queryGet {
	my ($barTitle, $barContent, $pageContent, $contentType);
	if (defined (param('ds')) && defined (param('de'))) {
		# start and end defined
		my ($fileContent, $time) = $connection->wumpus_get(param('ds'), param('de'));

		if ($bframe) {
			if (defined param('source')) {
				($pageContent, $contentType) = $connection->wumpus_getfile(param('fn'));
			} else {
				($pageContent, $contentType) = renderContent($fileContent);
			}
		} else {
			$barTitle = "File Content - " . (param('fn'));
			$barContent = param('dt');
		}
	}
	return ($pageContent, $barTitle, $barContent, undef, $contentType);
}

################################################################################
# &renderContent
################################################################################
sub renderContent {
	my ($fileContent) = @_;
	my $contentType;
	if (defined param('dt')) {
		if (param('dt') eq 'text/x-mail') {
			$contentType = 'text/plain';
			my @isoEncoding = $fileContent	=~ /^Content-Type:.*?; charset\s*=\s*(.*?)(;.*)*$/m;
			my ($header, $body) = $fileContent =~ /^(.*?From .*?\n\n)(.*)$/ms;

			if (defined $header) {
				if (defined ($isoEncoding[0])) {
					$body = decodeToUTF($body, $isoEncoding[0]);
				}
				$header = decodeMailHeader($header);

				$fileContent = $header . $body;
			}
		} elsif (param('dt') eq 'text/html') {
			my @encoding = $fileContent =~ /<(?:meta|META).*?charset(.*?)>/;
			my $isoEncoding = 'ISO-8859-1';
			if (defined $encoding[0]) {
				@encoding = $encoding[0] =~ /([\w-]+)/;
				$isoEncoding = $encoding[0];
			}
			$fileContent = decodeToUTF($fileContent, $isoEncoding);
		}
	} else {
	}
	return ($fileContent, $contentType);
}

################################################################################
# &querySearch
# Get search results from indexing server
################################################################################
sub querySearch {
	# tokenize the searchfield properly
	my $searchstring_ = formatSearchString();
	my ($start, $end, $barTitle, $barContent, $pageContent);

	# check for filetypes etc...
	$searchstring_ = " " . $searchstring_;

	if ($paramFileName ne '') {
		my $filename = param('filename');
		$filename =~ s/&//g;
		$filename =~ s/\[//g;
		$filename =~ s/\]//g;
		$searchstring_ = "[filename=$filename]" . $searchstring_;
	}

	if ($paramFileType ne '') {
		if (param('searchtype') eq 'files') {
			$searchstring_ = "\"<file!>\"..\"</file!>\" by" . $searchstring_;
		} else {
			$searchstring_ = "[filetype=$paramFileType]" . $searchstring_;
		}
	}

	if (defined (param('searchtype[]')) && param('searchtype[]') ne '') {
		my @filetype = param('searchtype[]');
		foreach (@filetype) {
			$searchstring_ = "[filetype=$_]" . $searchstring_;
		}
	}

	$barTitle = "Search Results";
	if (defined (param('searchtype'))) {
		if (defined ($searchTypeToDescription{param('searchtype')})) {
			$barTitle .= " (" . $searchTypeToDescription{param('searchtype')} . ")";
		}
	}

	if (defined (param('searchtype[]'))) {
		my @types = param('searchtype[]');
		my @desc;
		foreach (@types) {
			if (defined ($fileTypeToDescription{$_})) {
				push @desc, $fileTypeToDescription{$_};
			}
		}
		$barTitle .= " (" . (join ', ', @desc) . ")";
	}
	
	# do the search
	my ($time, $count, $index, @search_results) = sendSearchQuery($searchstring_);

	# check the result of the search
	if ($count == 0) {
		$pageContent .= <<END;
<br>
<table cellpadding="2" cellspacing="2" border="0" width="790">
<tr><td>
Wumpus was unable to find any documents that match your query. Are you sure
that what you are looking for exists on this machine? You might also want
to check your spelling.
</td></tr>
</table>
END
		$barContent = "No matching documents found ($time sec).";
	} else {
		$pageContent .= start_table({-border=>"0", cellpadding=>"2", -cellspacing=>"0"});
		$pageContent .= "\n\n";

		my @results;
		my $trContent;

		foreach my $i (@search_results) {
			push @results, processResult($i);
		}

		foreach my $i (@results) {
			$pageContent .= Tr($i . p);
			$pageContent .= "\n\n";
		}

		$pageContent .= end_table();
		$pageContent .= "\n\n";

		if ($count > 10) {
			$pageContent .= comment('page selection');
			$pageContent .= p({-align=>"center"}, generatePageSelection($count, $index));
		}

		$start = $index;
		$end = $start + $#search_results;

		$barContent = "Results " . ($start+1) . " - " . ($end+1) . " of $count ($time sec).";
	}

	return ($pageContent, $barTitle, $barContent);
}

################################################################################
# &queryStatistics
# Get statistics from the indexing server
################################################################################
sub queryStatistics {
	my @file_stat = split('\n', $connection->wumpus_filestat);
	my @summary = split('\n', $connection->wumpus_summary);

	my $pageContent = '';
	my ($fs, $n_files, $n_dir, $barTitle, $barContent);

	if ($#summary == 0) {
		$summary[0] =~ /(\S*)\s*(\S*) files\s*(\S*)/;
		$fs = $1;
		$n_files = $2;
		$n_dir = $3;
		$pageContent .= "The index covers " . b("$1") . ":<br>";
		$pageContent .= start_ul();
		$pageContent .= end_ul();

	} else {
		$pageContent .= "The index covers ";
		$pageContent .= b($#summary+1, " file systems:");

		$pageContent .= start_ul();
		foreach (@summary) {
			# TODO: Multiple file systems
			$_ =~ /(\S*)\s*(\S*) files\s*(\S*)/;
			$fs = $1;
			$n_files = $2;
			$n_dir = $3;
			$pageContent .= li(b("$n_files files"), "($n_dir directories) indexed for file system rooted at $fs");
		}
		$pageContent .= end_ul();
	}

	$pageContent .= "File type statistics (all files searchable by user $username)";

	$pageContent .= start_ul();
	foreach (@file_stat) {
		# TODO: Multiple file systems
		$_ =~ /^(.*?): (.*)$/;
		$pageContent .= li("$2 $fileTypeToDescription{$1} ($1)");
	}
	$pageContent .= end_ul();

	$barTitle = "Contents of Local Index";
	$barContent = "Index covers $fs, containing $n_files files and $n_dir directories.";
	return ($pageContent, $barTitle, $barContent);
}

################################################################################
# &highlightTerms
# Returns string with terms in param('searchstring') bolded
################################################################################
sub highlightTerms {
	my ($string_to_highlight) = @_;
	my $items_to_highlight = formatSearchString();

	my @items = $items_to_highlight =~ /"(.*?)"/g;

	foreach my $i (@items) {
		$string_to_highlight =~ s/($i)/<b>$1<\/b>/gi;
#		Alternative: highlight only entire words
#		$string_to_highlight =~ s/\b($i)\b/<b>$1<\/b>/gi;
	}
	return $string_to_highlight;
}

################################################################################
# &decodeMailHeader
# Returns string passed in UTF-8 encoding
################################################################################
use MIME::Words qw(:all);
use Text::Iconv;

sub decodeMailHeader {
	my ($string_to_decode) = @_;
	my $return;

	Text::Iconv->raise_error(1);

	eval {
		foreach my $i (decode_mimewords($string_to_decode)) {
			$return .= Text::Iconv->new($i->[1] || 'ISO-8859-1', 'UTF-8')->convert($i->[0]);
		}
	};

	# catch exception
	if ($@) {
		return Text::Iconv->new('ISO-8859-1', 'UTF-8')->convert($string_to_decode);
	} else {
		return $return;
	}
}

################################################################################
# &decodeToUTF
# Returns string passed in UTF-8 encoding
################################################################################
sub decodeToUTF {
	my ($string_to_decode, $type) = @_;
	my $return;

	Text::Iconv->raise_error(1);

	eval {
		$return = Text::Iconv->new($type, 'UTF-8')->convert($string_to_decode);
	};

	# catch exception
	if ($@) {
		#return Text::Iconv->new('ISO-8859-1', 'UTF-8')->convert($string_to_decode);
		return $string_to_decode;
	} else {
		return $return;
	}
}

################################################################################
# &stripHTML
################################################################################
sub stripHTML {
	my ($string) = @_;

	# simply html stripping: remove all tags, remove extraneous newline characters
	$string =~ s/<.*?>/ /g;
	$string =~ s/\n/ /g;
	$string =~ s/.*>//;
	$string =~ s/\<.*//;

	return $string;
}

################################################################################
# &timeToString
################################################################################
use POSIX qw(strftime);
sub timeToString {
	my ($time) = @_;
	return strftime "%e %b %Y", gmtime($time);
	#return strftime "%F %k:%M:%S", gmtime($time); # YYYY-MM-DD
	#return strftime "%b %e %Y %k:%M:%S", gmtime($time);
}

################################################################################
# &sizeToString
################################################################################
sub sizeToString {
	my ($size) = @_;
	my $exp = 0;
	my @exp_name = ('bytes', 'KB', 'MB', 'GB');
	while ($size / 1024 >= 1 && $exp < $#exp_name) {
		$size = $size / 1024;
		$exp++;
	}

	return (sprintf "%0.3f", $size) . " " . $exp_name[$exp];
}
# end of file
