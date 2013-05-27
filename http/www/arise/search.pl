#!/usr/bin/perl
use strict;
use warnings;

use WumpusConnection;
use CGI qw/-no_xhtml -oldstyle_urls :standard *ul *il *table/;
use POSIX;
use Crypt::Lite;
#use URI::Escape;
$| = 1;

################################################################################
# Globals
################################################################################
my $advancedFlag = (defined param('advanced'));
my $tframe = (defined param('tframe'));
my $bframe = (defined param('bframe'));
my $absURL= url(-absolute=>1);
my $query = new CGI('');
################################################################################
# Constants
################################################################################
my $defaultCharSet = 'ISO-8859-1';
my $lengthOfSnippet = 30;
my $lengthOfFilename = 64;

################################################################################
# Page generation constants
################################################################################
my $pageTitle = "ARISE Audio Search";
my $pageColor = "#FFFFFF";
my $pageStyle = <<END;
body,td,div,.p,a {
	font-family:arial,
	sans-serif
}
END
my $onLoad = "function fs() { document.form.sf.focus(); }\n";

my $formMethod = 'GET';
my @fileType = ( "text/html", "application/pdf", "application/postscript", "text/plain", "application/msword", "text/xml", "text/x-mail", "audio/mpeg", "application/multitext");
my %fileTypeToDescription = (
	"text/html" => "HTML documents",
	"application/pdf" => "Adobe PDF documents",
	"application/postscript" => "Adobe PostScript documents",
	"text/plain" => "Text files",
	"application/msword" => "Microsoft Office documents",
	"text/xml" => "XML documents",
	"text/x-mail" => "E-mail messages",
	"audio/mpeg" => "MPEG audio files",
	"application/multitext" => "MultiText input files"
);
my %searchTypeToDescription = (
	"everything" => "Everything",
	"text/x-mail" => "E-mail messages",
	"office" => "Office documents",
	"files" => "Files",
	"arise" => "Arise"
);
my %source = (
	"text/html" => "true",
	"text/plain" => "true",
	"text/x-mail" => "true",
	"text/xml" => "true"
);

################################################################################
# init -- establish connection with indexing server
################################################################################
my $crypt = Crypt::Lite->new();
my $connection = WumpusConnection->new();

#if ($connection->{connected} == 0) {
#	print redirect('login.pl');
#	exit;
#}

#my ($username, $password) = split(':', $crypt->decrypt(cookie('ssid'), $ENV{CL_HASH}));

#if ($connection->wumpus_connect($username, $password) != 1) {
#	print redirect('login.pl');
#	exit;
#}

################################################################################
# ---------- HTML generation starts here ---------- 
################################################################################
my ($pageContent, $infobarTitle, $infobarContent, $frame, $contentType, $charset) = processQuery();

$contentType = 'text/html' if (!(defined $contentType));
$charset = 'UTF-8' if (!(defined $charset));
$pageContent = '' if (!(defined $pageContent));

print header(-charset=>$charset, -content_type=>$contentType, -expires=>'-1d');

if ($frame || $bframe) {
	print $pageContent;
	exit;
}

print start_html(
	-title=>$pageTitle,
	-style=>{-code=>$pageStyle},
	-BGCOLOR=>$pageColor,
	-head=>base({-target=>'_top'}),
	-script=>$onLoad,
	-onLoad=>'fs()');
print "\n";

# print body header
print comment ('search box'), "\n";
print bodySearchBox();
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
	foreach (param('sf') =~ /(?:"(.*?)")|(\S*)/g) {
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

	my $title = \&titleArise;
	my $content = \&contentArise;
	my $header = \&headerArise;
	my $footer = \&footerArise;
	my $img = img({-src=>'audio.png'});

	# image
	return table({-cellspacing=>2, -cellpadding=>2, -width=>"800"},
		colgroup(col({-width=>"1%"}) . col({-width=>"100%"})) . 
		Tr([
			# title
			td({-colspan=>'2'}, [
				font({-size=>"+1"}, a({-href=>&arise_getHREF($result_string)}, b($title->($result_string))))
			]),
			# header & snippet
			td({-colspan=>'2'}, [
				$header->($result_string)
			]),
			(	
				td($img) . td(&highlightTerms($content->($result_string)))
			),
			# footer
			td({-colspan=>'2'}, [
				font({-size=>"-1"}, $footer->($result_string))
			])
		])
	);
}

################################################################################
sub getHREF {
	my ($result_string) = @_;
	my $doc_start = ($connection->wumpus_extract($result_string, "document_start"))[0];
	my $doc_end = ($connection->wumpus_extract($result_string, "document_end"))[0];
	my $filename = ($connection->wumpus_extract($result_string, "filename"))[0];
	my $document_type = ($connection->wumpus_extract($result_string, "document_type"))[0];
	my $filesize = ($connection->wumpus_extract($result_string, "filesize"))[0];
	my $timeModified = ($connection->wumpus_extract($result_string, "modified"))[0];
	my $uid = ($connection->wumpus_extract($result_string, "owner"))[0];
	my $gid = ($connection->wumpus_extract($result_string, "group"))[0];

	#my $query = new CGI('');
	$query->delete_all();
	$query->param('sf', param('sf')) if defined param('sf');
	$query->param('ds', $doc_start);
	$query->param('de', $doc_end);
	$query->param('fn', $filename);
	$query->param('dt', $document_type);
	$query->param('fs', $filesize);
	$query->param('mod', $timeModified);
	$query->param('uid', $uid);
	$query->param('gid', $gid);
	$query->param('advanced', '') if $advancedFlag;

	return $query->url(-absolute=>1, -query=>1);
}

################################################################################
# &trimSnippet
#
# Requires passage to be marked with #^passage^# and #^/passage^# tags
#
# Returns snippet of approximately $n_tokens tokens
################################################################################
sub trimSnippet {
	my ($snippet) = @_;
	my $n_tokens = $lengthOfSnippet;
	my ($prepassage, $passage, $postpassage);

	if ($snippet =~ /#\^passage\^#/) {
		my (@pre, @pass, @post);
		my ($pre_token, $post_token);
		my ($pre_space, $post_space);

		@pre = split ('#\^passage\^#', $snippet);
		@pass = split ('#\^/passage\^#', $pre[1]);
		$passage = $pass[0];

		$pre_space = ($pre[0] =~ /(\s*)$/)[0];
		$pre_space .= ($pass[0] =~ /^(\s*)/)[0];

		$post_space = ($pass[0] =~ /(\s*)$/)[0];
		$post_space .= ($pass[1] =~ /^(\s*)/)[0];

		@post = split (' ', $pass[1]);
		@pass = split (' ', $pass[0]);
		@pre = split (' ', $pre[0]);

		if ($n_tokens > $#pass) {
			my $n_tokens_needed = $n_tokens - $#pass;
			if ($n_tokens_needed > $#pre + $#post) {
				$pre_token = $#pre;
				$post_token = $#post;
			} else {
				if ($n_tokens_needed / 2 > $#pre) {
					$pre_token = $#pre;
					$postpassage = '...';
					$prepassage= '';
				} elsif ($n_tokens_needed / 2 > $#post) {
					$pre_token = $n_tokens_needed - $#post;
					$prepassage = '...';
					$postpassage = '';
				} else {
					$pre_token = int ($n_tokens_needed/2);
					$prepassage = '...';
					$postpassage = '...';
				}
				$post_token = $n_tokens_needed - $pre_token;
			}
		}

		$prepassage .= join (' ', @pre[-$pre_token .. -1]);
		$passage = join (' ', @pass);
		$postpassage = join (' ', @post[0 .. $post_token]) . $postpassage;

		$snippet = $prepassage . $pre_space . $passage . $post_space . $postpassage;
	} else {
		# passage tag not in text
		$snippet = "";
	}

	return $snippet;
}

################################################################################
# Arise Fn
################################################################################
sub titleArise {
	my ($result_string) = @_;
	my $snippet = ($connection->wumpus_extract($result_string, "headers"))[0];
	return &arise_extract($snippet, 'audioname');
}

sub headerArise {
	my ($result_string) = @_;
	my $snippet = ($connection->wumpus_extract($result_string, "headers"))[0];
	my $header;
	
	$header = start_table();
	$header .= Tr(td({-valign=>'top'}, b("Speaker: ")) . td(&highlightTerms(&arise_extract($snippet, 'speaker'))));
	$header .= Tr(td({-valign=>'top'}, b("Keywords: ")) . td(&highlightTerms(&arise_extract($snippet, 'keywords'))));

	my (%start, %end);
	my ($st, $et);
	arise_extract_hash(&arise_extract($snippet, 'startingtime'), \%start);
	arise_extract_hash(&arise_extract($snippet, 'endingtime'), \%end);
	$st = "$start{'month'} $start{'date'} $start{'year'} $start{'hour'}:$start{'minutes'}:$start{'seconds'}";
	$et = "$end{'month'} $end{'date'} $end{'year'} $end{'hour'}:$end{'minutes'}:$end{'seconds'}";

	$header .= Tr(td({-valign=>'top'}, b("Timestamp: ")) . td(&highlightTerms("$st - $et")));
	$header .= end_table();

	return $header;
}

sub contentArise {
	my ($result_string) = @_;
	my $snippet = ($connection->wumpus_extract($result_string, "headers"))[0];
	my $content;

	$content = &arise_extract($snippet, 'Dtranscription');

	return $content;
}

sub footerArise {
	my ($result_string) = @_;
	my $score = ($connection->wumpus_extract($result_string, "score"))[0];

	my $footer =
		"Score $score" . " - " . a({-href=>&getHREF}, "view raw text") .
		" - " . a({-href=>&arise_getHREF($result_string)}, "single audio stream") . 
		" - " . a({-href=>&arise_getHREF($result_string) . "_ALL"}, "multiple audio stream") . br .
		a({-href=>&arise_getHREF_Extended($result_string)}, "longer single audio stream") . 
		" - " . a({-href=>&arise_getHREF_Extended($result_string) . "_ALL"}, "longer multiple audio stream") . 
		br;
	return $footer;
}

sub arise_extract {
	my ($string, $term) = @_;
	return ($string =~ /<$term>(.*?)<\/$term>/s)[0];
}

sub arise_extract_hash {
	my ($string, $hash) = @_;
	%$hash = $string =~ /<(.*?)>(.*?)<\/\1>/gms;
}

sub arise_getHREF {
	my ($result_string) = @_;
	my $snippet = ($connection->wumpus_extract($result_string, "headers"))[0];

	my $name = &arise_extract($snippet, 'audioname');
	my $speaker = &arise_extract($snippet, 'speaker');
	my $start_offset = &arise_extract($snippet, 'startoffset');
	my $length = &arise_extract($snippet, 'length');

	my $filename = "$name\_$speaker\_$start_offset\_$length";

	my $url = url(-absolute=>1);
	$url .= "/$filename.wav\?&audio=$filename";

	return $url;
}

sub arise_getHREF_Extended {
	my ($result_string) = @_;
	my $snippet = ($connection->wumpus_extract($result_string, "headers"))[0];

	my $name = &arise_extract($snippet, 'audioname');
	my $speaker = &arise_extract($snippet, 'speaker');
	my $start_offset = &arise_extract($snippet, 'startoffset');
	my $length = &arise_extract($snippet, 'length');
	$start_offset -= 15;
	$length += 30;

	my $filename = "$name\_$speaker\_$start_offset\_$length";

	my $url = url(-absolute=>1);
	$url .= "/$filename.wav\?&audio=$filename";

	return $url;
}

sub queryAudio {
	my ($pageContent, $contentType, $charset);
	($pageContent, $contentType, $charset) = $connection->wumpus_getaudio(param('audio'));
	return ($pageContent, undef, undef, 1, $contentType, $charset);
}

################################################################################
# Generate links to other pages of results
################################################################################
sub generatePageSelection {
	my ($count, $index) = @_;
	my ($start, $end);
	my ($prevPage, $nextPage, $return);
	#my $query = new CGI('');
	$query->delete_all();

	my $cp = POSIX::floor($index/10);
	my $np = POSIX::floor($count/10);
	my $low = $cp - 4;
	my $high = $cp + 5;

	if ($high > $np) {
		$low -= $high - $np;
		$high = $np;
	}

	if ($low < 0) {
		$high += -$low;
		$high = $np if $high > $np;
		$low = 0;
	}

	$query->param('sf', param('sf')) if defined param('sf');
	$query->param('fn', param('fn')) if defined param('fn');
	$query->param('searchtype', param('searchtype')) if defined param('searchtype');
	foreach (param('searchtype[]')) {
		$query->append('searchtype[]', $_);
	}
	$query->param('advanced', '') if $advancedFlag;

	$prevPage = $nextPage = '';
	for (my $i = $low; $i <= $high; $i++) {
		$start = ($i * 10);
		$end = (($i + 1) * 10)-1;
		
		$query->param('start', $start);
		$query->param('end', $end);

		# create a << link to previous page
		if ($cp != $low && $i == $cp-1) {
			$prevPage = a({-href=>$query->url(-absolute=>1, -query=>1)}, "<<") . "&nbsp;";
		}

		# don't make a link to the current page
		if ($i != $cp) {
			$return .= a({-href=>$query->url(-absolute=>1, -query=>1)}, $i+1);
		} else {
			$return .= $i+1;
		}

		# create a >> link to next page
		if ($cp != $high && $i == $cp+1) {
			$nextPage = "&nbsp;" . a({-href=>$query->url(-absolute=>1, -query=>1)}, ">>");
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

	my $logo = a({-href=>$absURL}, img({-src=>"/arise/arise_logo.gif", -border=>"0"}));
	my $query_submit_button = submit(-label=>"Submit");

	my $searchfield = textfield(
		-name=>'sf',
		-size=>56,
		-maxlength=>256
	);

	$return .= start_form(-method=>$formMethod, -action=>$absURL, -name=>"form");
	$return .= hidden(-name=>'searchtype', value=>'arise');
	$return .= table(
		td([
			table([Tr([
				td({-valign=>"top", -align=>"center"}, $logo)
				])
			]),
			table(
				Tr([
					td(["Search query:", $searchfield, $query_submit_button])
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

	#$return .= table({-width=>"100%", -BGCOLOR=>"#E5ECF9", -border=>"0", -cellpadding=>"0", -cellspacing=>"0"},
	$return .= table({-width=>"100%", -border=>"0", -cellpadding=>"0", -cellspacing=>"0"},
		Tr(
			td(
				{-BGCOLOR=>"#E0F0FF", -nowrap=>'true', -width=>"10"}, ''
			),
			td(
				#{-BGCOLOR=>"#E0F0FF", -nowrap=>'true'}, [font({-size=>"+1"}, $infobarTitle)]
				{-BGCOLOR=>"#E0F0FF", -nowrap=>'true'}, $infobarTitle
			),
			td(
				{-BGCOLOR=>"#E0F0FF", -align=>"right", -nowrap=>'true'}, font({-size=>"-1"}, $infobarContent)
			),
			td(
				{-BGCOLOR=>"#E0F0FF", -nowrap=>'true', -width=>"10"}, ''
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
			$query = \&queryGetTopFrame;
		} elsif ($bframe) {
			$query = \&queryGetBottomFrame;
		} else {
			$query = \&queryGetFrame;
		}
	} elsif (defined (param('sf')) && param('sf') =~ /\w/) {
		$query = \&querySearch;
	} elsif (defined (param('audio'))) {
		$query = \&queryAudio;
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
	my $query = new CGI();
	$url = $query->url(-absolute=>1, -query=>1);

	$ratio = ($advancedFlag ? "250, 1*" : "165, 1*");

	if (defined (param('source'))) {
		my $fn = (split ('/', param('fn')))[-1];
		$b_url = $url;
		$b_url =~ s/\?/\/$fn\?/;
	} else {
		$b_url = $url;
	}

	my $page = <<END;
<FRAMESET rows="$ratio">
	<FRAME src="$url&tframe=" frameborder=1 scrolling=auto noresize>
	<FRAME src="$b_url&bframe=" frameborder=0>
</FRAMESET>
END
	return ($page, '', '', 1);
}

################################################################################
# &queryGetTopFrame
# Display file information
################################################################################
sub queryGetTopFrame {
	my ($barTitle, $barContent, $pageContent, $contentType, $charset);

	if (defined param('source')) {
		$barTitle = b("File Source - " . (param('fn')));
	} else {
		$barTitle = b("File Content - " . (param('fn')));
	}

	$barTitle .= br . font({-size=>"-1"}, 'Owner: ' . getpwuid(param('uid'))) if getpwuid(param('uid'));
	$barTitle .= font({-size=>"-1"}, ' - Group: ' . getgrgid(param('gid'))) if getgrgid(param('gid'));

	$barContent = "File Type: " . param('dt') . " - " . &sizeToString(param('fs')) . br;
	$barContent .= "Last Modified: " . &timeToTimeDate(param('mod'));
	
	return ($pageContent, $barTitle, $barContent, undef, $contentType, $charset);
}

################################################################################
# &queryGetBottomFrame
# Get file contents from indexing server
################################################################################
sub queryGetBottomFrame {
	my ($pageContent, $contentType, $charset);
	if (defined param('source')) {
		if (param('dt') eq 'text/x-mail') {
			my ($fileContent, $time) = $connection->wumpus_get(param('ds'), param('de'));
			$pageContent = $fileContent;
			$contentType = 'text/plain';
		} else {
			($pageContent, $contentType, $charset) = $connection->wumpus_getfile(param('fn'));
			if (defined $source{param('dt')}) {
				$contentType = 'text/plain';
			}
		}
	} else {
		my ($fileContent, $time) = $connection->wumpus_get(param('ds'), param('de'));
		($pageContent, $contentType, $charset) = renderContent($fileContent);
		$pageContent = table({-border=>"0", cellpadding=>"5", -cellspacing=>"5", -width=>"100%"}, Tr(td(pre(escapeHTML($pageContent)))));
		$contentType = 'text/html';
	}
	return ($pageContent, undef, undef, undef, $contentType, $charset);
}

################################################################################
# &renderContent
################################################################################
sub renderContent {
	my ($fileContent) = @_;
	my ($contentType, $charset);
	if (defined param('dt')) {
		if (param('dt') eq 'text/x-mail') {
			($fileContent, $contentType) = &renderXMail($fileContent);
		} elsif (param('dt') eq 'text/html') {
			my @encoding = $fileContent =~ /<(?:meta|META).*?charset(.*?)>/;
			my $isoEncoding = $defaultCharSet;
			if (defined $encoding[0]) {
				@encoding = $encoding[0] =~ /([\w-]+)/;
				$isoEncoding = $encoding[0];
			}
			$fileContent = decodeToUTF($fileContent, $isoEncoding);
		} elsif (param('dt') eq 'audio/mpeg') {
			$fileContent = &renderAudio($fileContent);
		} elsif (param('dt') eq 'application/pdf' || param('dt') eq 'application/postscript') {
			$charset = 'LATIN1';
			$contentType = 'text/plain';
		} else {
			$contentType = 'text/plain';
		}
	} else {
	}
	return ($fileContent, $contentType, $charset);
}

####################################################################################
# &querySearch
# Get search results from indexing server
################################################################################
sub querySearch {
	# tokenize the searchfield properly
	my $searchstring = formatSearchString();
	my ($start, $end, $barTitle, $barContent, $pageContent);

	# generate search string
	$searchstring = "\"<audio>\"..\"</audio>\" by " . $searchstring;

	$barTitle = b("Search Results");
	
	# do the search
	my ($time, $count, $index, @search_results) = sendSearchQuery($searchstring);

	# check the result of the search
	if (!(defined $count)) {
		$pageContent .= <<END;
<br>
<table cellpadding="2" cellspacing="2" border="0" width="790">
<tr><td>
Your search did not match any documents.<br>
</td></tr>
</table>
END
		$barContent = "No matching documents found ($time sec).";
	} else {
		$pageContent .= start_table({-border=>"0", cellpadding=>"5", -cellspacing=>"5"});

		my @results;
		my $trContent;

		foreach my $i (@search_results) {
			push @results, processResult($i);
		}

		foreach my $i (@results) {
			$pageContent .= Tr(td($i));
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

		if ($start == $end) {
			$barContent = "Results " . ($start+1) . " of $count ($time sec).";
		} else {
			$barContent = "Results " . ($start+1) . " - " . ($end+1) . " of $count ($time sec).";
		}
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
	$fs = $n_files = $n_dir = 0;

	my $i = ($#summary == 0 ? 1 : $#summary);

	# List of filesystems
	$pageContent .= "The index covers ";
	$pageContent .= b($i, ($i == 1 ? " file system:" : " file systems:"));

	$pageContent .= start_ul();
	foreach (@summary[-$i..-1]) {
		$_ =~ /(\S*)\s*(\S*) files\s*(\S*)/;
		$fs++;
		$n_files += $2;
		$n_dir += $3;
		$pageContent .= li(b("$2 files"), "($3 directories) indexed for file system rooted at $1");
	}
	$pageContent .= end_ul();
	$fs = $fs . ($i == 1 ? " file system" : " file systems");

	$pageContent .= "File type statistics";
	# (all files searchable by user $username)";

	# List of filetypes
	$pageContent .= start_ul();
	foreach (@file_stat) {
		$_ =~ /^(.*?): (.*)$/;
		$i = $fileTypeToDescription{$1};
		$pageContent .= li($2 . " " . ($2 == 1 ? substr($i, 0, length($i)-1) : $i) . " ($1)");
	}
	$pageContent .= end_ul();

	# Place in table for margins
	$pageContent = table({-border=>"0", cellpadding=>"5", -cellspacing=>"5", -width=>"100%"}, Tr(td($pageContent)));

	# Content for info bar
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

	my $open = "<span style=background-color:#FFFF00;font-weight:bold>";
	my $close = "</span>";
	my @items = $items_to_highlight =~ /"(.*?)"/g;
	my %hashtable;

	foreach my $i (@items) {
		# whitespace characters
		$i =~ s/\p{InWWS}/ /g;
		foreach my $j (split ' ', $i) {
			$hashtable{$j} = '';
		}
	}

	foreach my $i (keys %hashtable) {
		# highlight whole words
		$string_to_highlight =~ s/\b($i)\b/$open$1$close/gi;
	}

	# extend tags over 'whitespace'
	$string_to_highlight =~ s/$close((?:\p{InWWS}| )*)$open/$1/g;

	return $string_to_highlight;
}

################################################################################
# &timeToDate
# &timeToTimeDate
#  Returns string form equivalent of numeric date passed
################################################################################
use POSIX qw(strftime);
sub timeToDate {
	my ($time) = @_;
	return strftime "%e %b %Y", gmtime($time);
}

sub timeToTimeDate {
	my ($time) = @_;
	return strftime "%e %b %Y %l:%M:%S %p" , gmtime($time);
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

	if ($size * 1024 < 10000 && $exp > 0) {
		$size = $size * 1024;
		$exp--;
	}

	if ($exp == 0) {
		(sprintf "%d", $size) . " " . $exp_name[$exp];
	} else {
		(sprintf "%0.3f", $size) . " " . $exp_name[$exp];
	}
}

################################################################################
# &extractTag
################################################################################
sub extractTag {
	my ($string, $hash, $order) = @_;
	%$hash = $string =~ /<(.*?)>(.*?)<\/\1>/gm;
	@$order = $string =~ /<(.*?)>.*?<\/\1>/gm;
}

################################################################################
# &trimFilename
################################################################################
sub trimFilename {
	my $max_length = $lengthOfFilename;
	my ($filename) = @_;
	my (@path) = split ('/', $filename);
	my (@head, @tail, $headIndex, $tailIndex);
	my $length = 0;

	$headIndex = 0;
	$tailIndex = 0;

	if ($#path > 1) {
		unshift (@tail, @path[$#path-1..$#path]);
		$length += length($path[$#path-1]) + length($path[$#path]);
		$tailIndex = 2;

		while ($length < $max_length && $headIndex + $tailIndex <= $#path) {
			push (@head, $path[$headIndex]);
			$length += length($path[$headIndex]);
			$headIndex++;
		}

		if ($headIndex + $tailIndex < $#path) {
			$filename = (join '/', @head) . '/.../' . (join '/', @tail);
		}
	}
	return $filename;
}

# end of file
