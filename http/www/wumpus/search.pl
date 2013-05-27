#!/usr/bin/perl
use strict;
# use warnings;

use FindBin;
use lib $FindBin::Bin;

use WumpusConnection;
use CGI qw/-no_xhtml -oldstyle_urls :standard *ul *il *table/;
use POSIX;
use Crypt::Lite;
use Time::HiRes qw (gettimeofday tv_interval);
use MIME::Words qw(:all);
use Text::Iconv;
use POSIX qw(strftime);
$| = 1;

my $highlightTime;
my $startTime = [gettimeofday];
################################################################################
# Globals
################################################################################
my $advancedFlag = (defined param('advanced'));
my $tframe = (defined param('tframe'));
my $bframe = (defined param('bframe'));
my $absURL= url(-absolute=>1);
my $query = new CGI('');
my $wumpusCopyright = "Copyright &copy; 2007 by Stefan B&uuml;ttcher";

################################################################################
# Constants
################################################################################
my $systemCodeset = 'ISO-8859-1';
eval {
	# grab the system codeset
        require I18N::Langinfo;
        I18N::Langinfo->import(qw(langinfo CODESET));
        $systemCodeset = langinfo(CODESET());
};

my $lengthOfSnippet = 30;
my $lengthOfFilename = 64;
my $lengthOfHeader = 72;

my $highlightOpen = "<span style=background-color:#FFFF00;font-weight:bold>";
my $highlightClose = "</span>";

my $NoMatchContent = <<END;
<br>
<table cellpadding="2" cellspacing="2" border="0" width="790">
<tr><td>
Wumpus was unable to find any documents that match your query. Are you sure
that what you are looking for exists on this machine? You might also want
to check your spelling.
</td></tr>
</table>
END

################################################################################
# Page generation constants
################################################################################
my $pageTitle = "Wumpus File System Search";
my $pageColor = "#FFFFFF";
my $pageStyle = <<END;
body,td,div,.p,a {
	font-family:arial,
	sans-serif
}
END
my $onLoad = "function fs() { document.form.sf.focus(); }\n";

my $formMethod = 'GET';
my @fileType = ( "text/html", "application/pdf", "application/postscript", "text/plain", "application/x-office", "text/xml", "text/x-mail", "audio/mpeg", "application/multitext");
my %fileTypeToDescription = (
	"text/html" => "HTML documents",
	"application/pdf" => "Adobe PDF documents",
	"application/postscript" => "Adobe PostScript documents",
	"text/plain" => "Text files",
	"application/x-office" => "Office documents",
	"text/xml" => "XML documents",
	"text/x-mail" => "E-mail messages",
	"text/x-trec" => "TREC documents",
	"audio/mpeg" => "MPEG audio files",
	"text/troff" => "man pages",
	"application/multitext" => "MultiText input files"
);
my %searchTypeToDescription = (
	"everything" => "Everything",
	"email" => "E-mail messages",
	"office" => "Office documents",
	"files" => "Files"
);
my %source = (
	"text/html" => "true",
	"text/plain" => "true",
	"text/x-mail" => "true",
	"text/xml" => "true",
	"text/x-trec" => "true"
);

#####################################
# connecting to the indexing server #
#####################################
my $crypt = Crypt::Lite->new();

# establish connection with indexing server
my $connection = WumpusConnection->new();

# verify that connection exists
if ($connection->{connected} == 0) {
	print redirect('login.pl');
	exit;
}

# decrypt the username/password
my ($username, $password) = split(':', $crypt->decrypt(cookie('ssid'), $ENV{CL_HASH}));

# authenticate against the server
if (defined ($ENV{WUMPUS_AUTH})) {
	if ($connection->wumpus_connect($username, $password) != 1) {
		print redirect('login.pl');
		exit;
	}
}

########
# main #
########

# process the request
my ($pageContent, $infobarTitle, $infobarContent, $frame, $contentType, $charset) = processQuery();

$contentType = 'text/html' if (!(defined $contentType));
$charset = 'UTF-8' if (!(defined $charset));
$pageContent = '' if (!(defined $pageContent));

print header(-charset=>$charset, -content_type=>$contentType, -expires=>'-1d');

if ($frame || $bframe) {
	print $pageContent;
	exit;
}

my $start_html = start_html(
	-title=>$pageTitle,
	-style=>{-code=>$pageStyle},
	-BGCOLOR=>$pageColor,
	-head=>base({-target=>'_top'}),
	-script=>$onLoad,
	-onLoad=>'fs()');
$start_html =~ s/<!DOCTYPE.*?>\n//s;

print $start_html;
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
print comment ('total time: ' . tv_interval($startTime)), "\n";

print comment (extractSearchTerms());
print comment ($systemCodeset);
print end_html;

############
# end main #
############

################################################################################
################################################################################
## Page Generation Functions
################################################################################
################################################################################

################################################################################
# &processQuery
#  Determines which subroutine to call depending on the parameters passed to the script
#
# Returns: &$query
#  &$query: result of a call to the subroutine pointed to by $query
#
# Subroutines called by processQuery are expected to return:
#  ($pageContent, $infobarTitle, $infobarContent, $frame, $contentType, $charset)
#  $pageContent: body of the page
#  $infobarTitle: data to be place in the left of the header bar
#  $infobarContent: data to be place in the right of the header bar
#  $frame: if defined $pageContent contains frame layout
#  $contentType: content type of $pageContent (i.e. text/html, text/plain)
#  $charset: character set of $pageContent
################################################################################
sub processQuery {
	my $query;
	if (defined (param('ds')) && param('ds') ne '') {
		if ($tframe) {
			$query = \&queryGetTopFrame;
		} elsif ($bframe) {
			$query = \&queryGetBottomFrame;
		} else {
			$query = \&queryGetFrame;
		}
	} elsif (defined (param('sf'))) { # && param('sf') =~ /\w/) {
			$query = \&querySearch;
	} else {
		$query = \&queryStatistics;
	}
	return &$query;
}

################################################################################
# &queryGetFrame
#  Generates frame layout page for a document retrieval request
#
# See &processQuery for return values
################################################################################
sub queryGetFrame {
	my ($url, $b_url, $ratio);
	my $query = new CGI();
	$url = $query->url(-absolute=>1, -query=>1);

	$ratio = ($advancedFlag ? "250, 1*" : "165, 1*");
	my $page = <<END;
<HTML>
<TITLE>$pageTitle</TITLE>
<FRAMESET rows="$ratio">
	<FRAME src="$url&tframe=" frameborder=0 scrolling=auto noresize>
	<FRAME src="$url&bframe=" frameborder=0>
	</FRAMESET>
</FRAMESET>
</HTML>
END
	return ($page, '', '', 1);
}

################################################################################
# &queryGetTopFrame ()
#  Generates file information for document retrieval request from parameters 
#
# See &processQuery for return values
################################################################################
sub queryGetTopFrame {
	my ($barTitle, $barContent, $pageContent, $contentType, $charset);

	my ($filetype, $filename) = $connection->wumpus_fileinfo(param('ds'));

	if (defined $ENV{CRAWLER}) {
		$filename = extractURL($filename);
	}

	if (defined param('src')) {
		$barTitle = b("File Source - $filename");
	} else {
		$barTitle = b("File Content - $filename");
	}

	$barContent = "File Type: $filetype - " . &sizeToString(param('fsize'));

	if (!defined $ENV{CRAWLER}) {
		$barTitle .= br . font({-size=>"-1"}, 'Owner: ' . getpwuid(param('uid'))) if getpwuid(param('uid'));
		$barTitle .= font({-size=>"-1"}, ' - Group: ' . getgrgid(param('gid'))) if getgrgid(param('gid'));
		$barContent .= br . "Last Modified: " . &timeToTimeDate(param('mod'));
	}

	
	return ('', $barTitle, $barContent, undef, $contentType, $charset);
}

################################################################################
# &queryGetBottomFrame ()
#  Retrieves file contents from indexing server
#  Displays plain text of document if from 'View Source' link
#  Renders document if from title link
#
# See &processQuery for return values
################################################################################
sub queryGetBottomFrame {
	my ($pageContent, $contentType, $charset, $time);
	my ($filetype, $filename) = $connection->wumpus_fileinfo(param('ds'));

	my $getWholeFile = 1;
	if ((defined param('ds')) && (defined param('de')) && (defined param('fs')) && (defined param('fe'))) {
		if ((param('ds') ne param('fs')) || (param('de') ne param('fe'))) {
			$getWholeFile = 0;
		}
	}
	if ((defined param('ds')) && (!(defined $source{$filetype}) || $filetype eq 'text/x-mail')) {
		$getWholeFile = 0;
	}

	if ($getWholeFile) {
		($pageContent, $contentType, $charset) = $connection->wumpus_getfile($filename);
	} else {
		($pageContent) = $connection->wumpus_get(param('ds'), param('de'));
	}

	if (defined param('src')) {
		# View Source link -- display plain text of document
		if (defined $source{$filetype}) {
			$contentType = 'text/html';
			$charset = $systemCodeset;
			$pageContent = table({-border=>"0", cellpadding=>"5", -cellspacing=>"5", -width=>"100%"}, Tr(td("<pre wrap>" . escapeHTML($pageContent) . "</pre>")));
		}
	} else {
		# Title link -- render document
		($pageContent, $contentType, $charset) = renderContent($pageContent, $filetype);
	}
	return ($pageContent, undef, undef, undef, $contentType, $charset);
}

################################################################################
# &querySearch ()
#  Sends query to indexing server using parameters
#  Generates search result page using data returned and subroutine &processResults
#
# Returns: ($pageContent, $barTitle, $barContent);
#  See &processQuery for return values
################################################################################
sub querySearch {
	my $searchType;
	my $searchQuery;
	my ($start, $end, $barTitle, $barContent, $pageContent);

	# tokenize the searchfield properly
	my (@searchTerms) = extractSearchTerms();

	# generate search query
	if ($ENV{SEARCH_QUERY} ne '') {
		$searchType = $ENV{SEARCH_QUERY};
	} else {
		$searchType = qq{"<document!>".."</document!>"};
	}

	if ($advancedFlag) {
		my @searchQualifiers;
		if (defined (param('searchtype[]'))) {
			my @types = param('searchtype[]');
			if ($#types != -1) {
				push @searchQualifiers, "{filetype in " . join (', ', @types) . "}";
			}
		}

		if (defined param('sfn') && param('sfn') ne '') {
			my $filename = param('sfn');
			$filename =~ s/&//g;
			$filename =~ s/\[//g;
			$filename =~ s/\]//g;
			$filename =~ s/{//g;
			$filename =~ s/}//g;
			push @searchQualifiers, "{filepath = $filename}";
		}

		if (defined param('sfsl') && param('sfsl') ne '') {
			my $filesize = param('sfsl');
			if ($filesize !~ /\D/) {
				push @searchQualifiers, "{filesize >= $filesize}";
			}
		}

		if (defined param('sfsg') && param('sfsg') ne '') {
			my $filesize = param('sfsg');
			if ($filesize !~ /\D/) {
				push @searchQualifiers, "{filesize <= $filesize}";
			}
		}

		if ($#searchQualifiers != -1) {
			$searchQuery = "($searchType) > (" . join (' ^ ', @searchQualifiers) . ')';
		} else {
			$searchQuery = "$searchType";
		}
	} else {
		if (defined (param('searchtype'))) {
			if (param('searchtype') eq 'files') {
				$searchQuery = qq{"<file!>".."</file!>"};
			} elsif (param('searchtype') eq 'office') {
				$searchQuery = "($searchType) < ({filetype in application/pdf, application/postscript, application/x-office})";
			} elsif (param('searchtype') eq 'email') {
				$searchQuery = "($searchType) < ({filetype in text/x-mail})";
			} else {
				$searchQuery = $searchType;
			}
		} else {
			$searchQuery = $searchType;
		}
	}

	if ($searchTerms[0] ne '') {
		$searchQuery = "($searchQuery)>($searchTerms[0])"
	}

	if ($searchTerms[1] ne '') {
		$searchQuery = "($searchQuery)/>($searchTerms[1])"
	}

	$searchQuery = "$searchQuery by " . $searchTerms[2] . " with weights from ($searchType)";

	# send the search query
	if (defined $ENV{WUMPUS_DEBUG}) {
		my ($time, $count, $index, $raw_results, @search_results) = sendSearchQuery($searchQuery);
		return ("<pre wrap>" . escapeHTML($searchQuery . "\n\n" . $raw_results) . "</pre>", '', '');
	}

	my ($time, $count, $index, undef, @search_results) = sendSearchQuery($searchQuery);

	# generate the header bar
	$barTitle = b("Search Results");
	if ($advancedFlag) {
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
	} else {
		if (defined (param('searchtype'))) {
			if (defined ($searchTypeToDescription{param('searchtype')})) {
				$barTitle .= " (" . $searchTypeToDescription{param('searchtype')} . ")";
			}
		}
	}

	# check the result of the search
	if ($#search_results == -1) {
		$pageContent .= $NoMatchContent;
		$barContent = "No matching documents found ($time sec).";
	} else {
		# start a table
		$pageContent .= start_table({-border=>"0", cellpadding=>"5", -cellspacing=>"5"});

		# process each search query result, placing results in table
		foreach my $i (@search_results) {
			$pageContent .= Tr(td(&processResult($i)));
			$pageContent .= "\n\n";
		}

		$pageContent .= end_table();
		$pageContent .= "\n\n";

		# get page selection
		if (defined $count && $count > 10) {
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

	$pageContent = comment($searchQuery) . "\n" . $pageContent;

	return ($pageContent, $barTitle, $barContent);
}

################################################################################
# &queryStatistics
#  Queries the indexing server for statistics
#
# Returns: ($pageContent, $barTitle, $barContent)
#  See &processQuery for return values
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
	my @entry;
	foreach (@summary[-$i..-1]) {
		# $_ =~ /(\S*)\s*(\S*) files\s*(\S*)/;
		@entry = split ("\t", $_);
		$fs++;
		$n_files += (split (' ', $entry[1]))[0];
		$n_dir += (split (' ', $entry[2]))[0];
		$pageContent .= li(b("$entry[1]"), "($entry[2]) indexed for file system rooted at $entry[0]");
	}
	$pageContent .= end_ul();
	$fs = $fs . ($i == 1 ? " file system" : " file systems");

	$pageContent .= "File type statistics (all files searchable" . (defined($username) ? " by user <b>$username</b>)" : ")");

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
	$barTitle = b("Contents of Local Index");
	$barContent = "Index covers $fs, containing $n_files files and $n_dir directories.";
	return ($pageContent, $barTitle, $barContent);
}

################################################################################
################################################################################
##  &querySearch() Sub-Functions
################################################################################
################################################################################

################################################################################
# &sendSearchQuery ($search_string)
#  Sends the query $search_string to the indexing server
#
# Returns: ($time, $count, $index, @results)
#  $time: number of milliseconds taken to process query
#  $count: number of results that satisfy the query 
#  $index: rank of the first search result
#  @results: array of search result content
################################################################################
sub sendSearchQuery {
	my ($search_string) = @_;
	my ($result, $time) = $connection->wumpus_search($search_string, param('start') || 0, param('end') || 9);
	my @count = extractWumpusTag($result, "count");
	my @index = extractWumpusTag($result, "rank");
	my @results = extractWumpusTag($result, "document");
	return ($time, $count[0], $index[0], $result, @results);
}

################################################################################
# &extractSearchTerms
#  Extract search terms from param('sf')
#  Identify inclusion(+) and exclusion(-) search terms
#
# Returns: @array
#  $array[0]: GCL expression for inclusion terms
#  $array[1]: GCL expression for exclusion terms
#  $array[2]: comma-separated list of search terms
################################################################################
sub extractSearchTerms {
	my @tokens = split (' ', param('sf'));

	my @inclusion;
	my @exclusion;
	my @terms;

	my @current;

	foreach (@tokens) {
		push @current, $_;
		my $string = join (' ', @current);
		if ($string =~ /"(.*?)"/) {
			my $str = $1;
			$str =~ s/\p{InWWS_stem}/ /g;
			if ($str !~ /\S/) {
				# blank term
			} elsif ($string =~ /^\s*\+/) {
				push @inclusion, "\"$str\"";
				push @terms, "\"$str\"";
			} elsif ($string =~ /^\s*-/) {
				push @exclusion, "\"$str\"";
			} else {
				push @terms, "\"$str\"";
			}
			@current = ();
		} elsif ($string !~ /"/) {
			my $str = $string;
			$str =~ s/\p{InWWS_stem}/ /g;
			
			if ($str !~ /\S/) {
				# blank term
			} elsif ($string =~ s/^\s*\+//) {
				foreach my $i (split ' ', $str) {
					push @inclusion, "\"$i\"";
					push @terms, "\"$i\"";
				}
			} elsif ($string =~ s/^\s*-//) {
				foreach my $i (split ' ', $str) {
					push @exclusion, "\"$i\"";
				}
			} else {
				foreach my $i (split ' ', $str) {
					push @terms, "\"$i\"";
				}
			}
			@current = ();
		}
	}

	return (join ('^', @inclusion), join ('+', @exclusion), join (', ', @terms));
}

################################################################################
# &processResult
#  Generates a formatted HTML search result from the raw result data
#   containing the title, select data from the header,
#   a snippet of the relavant passage and file information of the document.
#  Uses &title__, &header__, &content__ and &footer__ subroutines based on the
#   document type
#
# Returns: $table
#  $table: the formatted HTML search result
################################################################################
sub processResult {
	my ($results) = @_;

	# take $results_string and extract tags into hash
	my %fields;
	&extractResults(\%fields, $results);

	# set the subroutines to call to extract data	
	my $title = \&titleGeneric;
	my $header = \&headerGeneric;
	my $content = \&contentGeneric;
	my $footer = \&footerGeneric;
	my $img = '';

	my $document_type = $fields{'document_type'};
	if ($document_type eq 'text/html') {
		$title = \&titleHTML;
		$header = \&headerHTML;
		$content = \&contentHTML;
		# extract the first character set from the META tag
		my @encoding = $fields{'headers'} =~ /<(?:meta|META).*?charset(.*?)>/s;

		if (defined $encoding[0]) {
			@encoding = $encoding[0] =~ /([\w-]+)/;
			$fields{'encoding'} = $encoding[0];
		}
	} elsif ($document_type eq 'text/xml') {
		$title = \&titleHTML;
		$content = \&contentHTML;
	} elsif ($document_type eq 'text/x-mail') {
		$title = \&titleXMail;
		$header = \&headerXMail;
		$content = \&contentXMail;
	} elsif ($document_type eq 'audio/mpeg') {
		$content = \&contentAudio;
	} elsif ($document_type eq 'text/x-trec') {
		$title = \&titleTREC;
		$header = \&headerTREC;
		$content = \&contentTREC;
	}

	# set the name of the image associated with the document type
	my $imgfile = $document_type;
	$imgfile =~ s/\//-/g;
	$imgfile = "/img/mime-$imgfile.png";
	$img = img({-src=>$imgfile});

	# return HTML table filled with processed query results
	return table({-cellspacing=>2, -cellpadding=>2, -width=>"800"},
		colgroup(col({-width=>"1%"}) . col({-width=>"100%"})) . 
		Tr([
			# title
			td({-colspan=>'2'}, [
				font({-size=>"+1"}, a({-href=>$fields{'title_HREF'}}, b(&highlightTerms($title->(\%fields)))))
			]),
			# header & snippet
			td({-colspan=>'2'}, [
				$header->(\%fields)
			]),
			(	
				td($img) . td(&highlightTerms($content->(\%fields)))
			),
			# footer
			td({-colspan=>'2'}, [
				font({-size=>"-1"}, $footer->(\%fields))
			])
		])
	);
}

################################################################################
# &extractResults (%fields, $results)
#  Extracts fields from the results and stores in hash %fields
################################################################################
sub extractResults {
	my ($fields, $results) = @_;
	$$fields{'doc_start'} = (extractWumpusTag($results, 'document_start'))[0];
	$$fields{'doc_end'} = (extractWumpusTag($results, 'document_end'))[0];
	$$fields{'file_start'} = (extractWumpusTag($results, 'file_start'))[0];
	$$fields{'file_end'} = (extractWumpusTag($results, 'file_end'))[0];
	$$fields{'filename'} = (extractWumpusTag($results, 'filename'))[0];
	$$fields{'document_type'} = (extractWumpusTag($results, 'document_type'))[0];
	$$fields{'filesize'} = (extractWumpusTag($results, 'filesize'))[0];
	$$fields{'timeModified'} = (extractWumpusTag($results, 'modified'))[0];
	$$fields{'uid'} = (extractWumpusTag($results, 'owner'))[0];
	$$fields{'gid'} = (extractWumpusTag($results, 'group'))[0];
	$$fields{'score'} = (extractWumpusTag($results, 'score'))[0];
	$$fields{'page'} = (extractWumpusTag($results, 'page'))[0];
	$$fields{'headers'} = (extractWumpusTag($results, 'headers'))[0];
	$$fields{'snippet'} = (extractWumpusTag($results, 'snippet'))[0];

	# generic title for result for use if a title cannot be extracted from the header
	$$fields{'title_filename'} = (split('/', $$fields{'filename'}))[-1];

	$$fields{'get_HREF'} = &titleHREF($fields);
	$$fields{'open_HREF'} = &openHREF($fields);

	if (defined $ENV{CRAWLER}) {
		$$fields{'title_HREF'} = &extractURL($$fields{'filename'});
		$$fields{'footer_filename'} = $$fields{'title_HREF'};
		if ($$fields{'title_filename'} eq '_._.html') {
			$$fields{'title_filename'} = $$fields{'title_HREF'};
		}
	} else {
		$$fields{'title_HREF'} = $$fields{'get_HREF'};
		$$fields{'footer_filename'} = trimFilename($$fields{'filename'});
	}
}

################################################################################
# &titleHREF ($fields)
#  Generate a URL for rendering the document
#
# Returns: $url
#  $url: a URL that will render the document
################################################################################
sub titleHREF {
	my ($fields) = @_;
	$query->delete_all();

	$query->param('sf', param('sf')) if defined param('sf');
	$query->param('ds', $$fields{'doc_start'});
	$query->param('de', $$fields{'doc_end'});
	$query->param('fs', $$fields{'file_start'});
	$query->param('fe', $$fields{'file_end'});
	$query->param('fsize', $$fields{'filesize'});
	$query->param('mod', $$fields{'timeModified'});
	$query->param('uid', $$fields{'uid'});
	$query->param('gid', $$fields{'gid'});
	$query->param('searchtype', param('searchtype'));
	$query->param('searchtype[]', param('searchtype[]'));
	$query->param('advanced', '') if $advancedFlag;

	return $query->url(-relative=>1, -query=>1);
}

################################################################################
# &openHREF ($fields)
#  Generate a URL for retrieving
#
# Returns: $url
#  $url: a URL that will retrieve the document
################################################################################
sub openHREF {
	my ($fields) = @_;
	$query->delete_all();
	
	$query->param('idx', $$fields{'doc_start'});

	my $url = $query->url(-relative=>1, -query=>1);
	$url =~ s/^.*?\?/retrieve.pl\/$$fields{'title_filename'}?/;
	return $url;
}

################################################################################
# &extractURL ($filename)
#  Generate a URL that references the original document on the web
#
# Returns: $url
#  $url: a URL that references the original document on the web
################################################################################
sub extractURL {
	my ($filename) = @_;
	my @path;

	if (defined $ENV{CRAWL_DIR} && $filename =~ /^$ENV{CRAWL_DIR}/) {
		$filename = substr($filename,length($ENV{CRAWL_DIR}) + 1);
		(@path) = split('/', $filename);
		if ($path[0] =~ /_[0-9]*$/) {
			$path[0] =~ s/_([0-9]*$)/:\1/;
			$path[0] =~ s/:80$//;
		}

		if ($path[-1] =~ /^_\._\.html$/) {
			$path[-1] = '';
		}
		return "http://" . join ('/', @path);
	} else {
		return "file://$filename";
	}

}

################################################################################
# &trimSnippet ($snippet)
#  Expects $snippet to consist of 3 lines: pre-passage, passage, post-passage
#  Generates from $snippet a string of at most $lengthOfSnippet tokens
#   consisting of 'passage' and as much of 'pre-passage' and 'post-passage'
#
# A token is a word containing a non-whitespace (\p{InWWS}) character.
#
# Returns: $snippet
#  $snippet: trimmed snippet on 1 line
################################################################################
sub trimSnippet {
	my ($snippet) = @_;
	my $n_tokens = $lengthOfSnippet;
	my ($prepassage, $passage, $postpassage);

	my (@pre, @pass, @post);
	my ($pre_token, $post_token);
	my ($pre_space, $post_space);

	my @parts = split ("\n", $snippet);

	$pre_space = ($parts[0] =~ /(\s*)$/)[0];
	$pre_space .= ($parts[1] =~ /^(\s*)/)[0];

	$post_space = ($parts[1] =~ /(\s*)$/)[0];
	$post_space .= ($parts[2] =~ /^(\s*)/)[0];

	# remove excess whitespace from pre and post passages
	$parts[0] =~ s/(\p{InWWS}| ){8,}/ /g;
	$parts[2] =~ s/(\p{InWWS}| ){8,}/ /g;

	@pre = split (' ', $parts[0]);
	@pass = split (' ', $parts[1]);
	@post = split (' ', $parts[2]);

	# determine the number of tokens in the passage
	my $pass_tokens = 0;
	foreach my $token (@pass) {
		if ($token =~ /\P{InWWS}/o) {
			# contains non-whitespace character
			$pass_tokens++;
		}
	}

	if ($n_tokens > $pass_tokens) {
		# we require tokens from @pre and @post to pad the passage
		my $n_tokens_needed = $n_tokens - $pass_tokens;
		if ($n_tokens_needed > $#pre + $#post) {
			# entire passage is shorter than maximum number of tokens
			$pre_token = $#pre;
			$post_token = $#post;
			$prepassage = '';
			$postpassage = '';
		} else {
			# entire passage is longer than maximum number of tokens
			# pad the passage with tokens from the end of @pre and the beginning of @post
			$pre_token = 0;
			$post_token = 0; 
			while ($n_tokens_needed > 0) {
				# find a token in @pre
				until ($pre_token >= $#pre || ($pre[-($pre_token+1)] =~ /\P{InWWS}/)) {
					$pre_token++;
				}
				if ($pre_token < $#pre) {
					$pre_token++;
					$n_tokens_needed--;
				}

				# find a token in @post
				until ($post_token >= $#post || ($post[$post_token] =~ /\P{InWWS}/)) {
					$post_token++;
				}
				if ($post_token < $#post) {
					$post_token++;
					$n_tokens_needed--;
				}

				# terminate if we've reached the beginning of @pre and end of @post
				# may occur if many words are considered non-tokens
				if ($pre_token >= $#pre && $post_token >= $#post) {
					$n_tokens_needed = 0;
				}
			}
		}
		# generate the strings consisting of the trimmed pre-passage and post-passage
		$prepassage = join (' ', @pre[-($pre_token+1) .. -1]) if ($#pre != -1);
		$postpassage = join (' ', @post[0 .. $post_token]) . $postpassage if ($#post != -1);
		# add '...' if the pre-passage or post-passage had to be trimmed
		$prepassage = '...' . $prepassage if ($#pre != $pre_token);
		$postpassage = $postpassage . '...' if ($#post != $post_token);
	}
	$passage = join (' ', @pass);

	$snippet = $prepassage . $pre_space . $passage . $post_space . $postpassage;

	my $n_chars = 300;
	if (length($snippet) > $n_chars) {
		# trim from the edges -- keep the passage
		$n_chars -= length($passage);

		if (length($prepassage) < ($n_chars / 2) || (length($postpassage) < ($n_chars /2))) {
			if (length($prepassage) < length($postpassage)) {
				$snippet = $prepassage . $pre_space . $passage . $post_space . substr($postpassage, 0, $n_chars - length($prepassage)) . '...';
			} else {
				$snippet = '...' . substr($prepassage,-($n_chars-length($postpassage))) . $pre_space . $passage . $post_space . $postpassage;
			}
		} else {
			$snippet = '...' . (substr($snippet, (length($snippet)/2)-150,300)) . '...';
		}
	}
	return $snippet;
}



################################################################################
# &generatePageSelection ($count, $index)
#  Generates page selection links using $count, the number of results
#   and $index, the rank of the first query result on the page
#
# Returns: $links
#  $links: HTML string that consists of links to other pages of results
################################################################################
sub generatePageSelection {
	my ($count, $index) = @_;
	my ($start, $end);
	my ($prevPage, $nextPage, $links);
	$query->delete_all();

	my $cp = POSIX::floor($index/10);
	my $np = POSIX::floor(($count-1)/10);
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
	$query->param('sfn', param('sfn')) if defined param('sfn');
	$query->param('searchtype', param('searchtype')) if defined param('searchtype');
	foreach (param('searchtype[]')) {
		$query->append('searchtype[]', $_);
	}
	$query->param('sfsl', param('sfsl')) if defined param('sfsl');
	$query->param('sfsg', param('sfsg')) if defined param('sfsg');
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
			$links .= a({-href=>$query->url(-absolute=>1, -query=>1)}, $i+1);
		} else {
			$links .= $i+1;
		}

		# create a >> link to next page
		if ($cp != $high && $i == $cp+1) {
			$nextPage = "&nbsp;" . a({-href=>$query->url(-absolute=>1, -query=>1)}, ">>");
		}
		$links .= "\n";
	}

	return $prevPage . $links. $nextPage;
}


################################################################################
################################################################################
##  start &processResult functions
##  Functions extract data from the %fields hash
##  Functions return HTML strings to be used in search result page
################################################################################
################################################################################

################################################################################
# &titleGeneric ($fields)
#
# Returns: $title
#  $title: title of the document
################################################################################
sub titleGeneric {
	my ($fields) = @_;
	return $$fields{'title_filename'};
}

################################################################################
# &headerGeneric ($fields)
#
# Returns: ''
#  No information is extracted
################################################################################
sub headerGeneric {
	return '';
}

################################################################################
# &footerGeneric($fields)
#
# Returns: $footer
#  $footer: contains the following:
#	-filename
#	-score of document
#	-page numbers in which passage is found
#	-filesize
#	-timestamp
#	-document type
#	-a link to view the file source or open the file
#	-a link to render the file if operating in crawler mode
################################################################################
sub footerGeneric {
	my ($fields) = @_;

	my $footer = $$fields{'footer_filename'} . " - " . $$fields{'document_type'} . br;
	$footer .= "Score " . $$fields{'score'};

	if (defined $$fields{'page'} && $$fields{'page'} != 1) {
		if ($$fields{'page'} =~ /-/) {
			$footer .= " - Pages " . $$fields{'page'};
		} else {
			$footer .= " - Page " . $$fields{'page'};
		}
	}

	if (defined $$fields{'timeModified'}) {
		$footer .= " - " . &timeToDate($$fields{'timeModified'});
	}

	if (defined $$fields{'filesize'}) {
		$footer .= " - " . &sizeToString($$fields{'filesize'});
	}

	if (defined $ENV{CRAWLER}) {
		$footer .= " - " . a({-href=>$$fields{'get_HREF'}}, "Cached");
	}

	if (defined $source{$$fields{'document_type'}}) {
		$footer .= " - " . a({-href=>$$fields{'get_HREF'}."&src="}, "View Source");
	} else {
		$footer .= " - " . a({-href=>$$fields{'open_HREF'}}, "Open File");
	}

	return $footer;
}

################################################################################
# &contentGeneric ($fields)
#
# Returns: $content
#  $content: a snippet containing the relavant passage
################################################################################
sub contentGeneric {
	my ($fields) = @_;
	my $snippet = $$fields{'snippet'};

	if (defined($snippet)) {
		# decode the snippet
		$snippet = decodeToUTF($snippet);

		# remove all newline characters and replace the <passage!> tags with \n
		$snippet =~ s/\n/ /g;	
		$snippet =~ s/<passage!>/\n/g;
		$snippet =~ s/<\/passage!>/\n/g;

		$snippet = escapeHTML(trimSnippet($snippet));
	} else {
		$snippet = '';
	}

	return $snippet;
}

################################################################################
# &titleHTML ($fields)
#
# Returns: $title
#  $title: title of the HTML document from <title>..</title> tags or
#          filename if tags are not present
################################################################################
sub titleHTML {
	my ($fields) = @_;
	my $filename = $$fields{'title_filename'};
	my $headers = $$fields{'headers'};
	my $title = (extractHTML($headers, "title"))[0] || $filename;

	return decodeToUTF($title, $$fields{'encoding'});
}

################################################################################
# &headerHTML ($fields)
#
# Returns: $header
#  $header: name/content pairs extracted from the META tag
#           if the content contains a search term
################################################################################
sub headerHTML {
	my ($fields) = @_;
	my $headers = $$fields{'headers'};
	my $content;

	# extract from meta tags
	my @meta_array = $headers =~ /<meta(.*?)>/gsi;
	my %meta;

	foreach my $i (@meta_array) {
		$i =~ /name="(.*?)".*?content="(.*?)"/si;
		$meta{$1} = $2;
	}

	foreach my $i (keys %meta) {
		# determine if we have highlighted content in the content
		$meta{$i} = trimEndString($meta{$i},$lengthOfHeader);
		$meta{$i} = highlightTerms($meta{$i});
		if ($meta{$i} =~ /$highlightOpen/) {
			$content .= b(ucfirst($i)) . ': ' . $meta{$i} . br;
		}
	}

	return $content;
}	

################################################################################
# &contentHTML ($fields)
#
# Returns: $content
#  $content: a snippet containing the relavant passage
################################################################################
sub contentHTML {
	my ($fields) = @_;
	my $snippet = $$fields{'snippet'};
	my $headers = $$fields{'headers'};

	# decode the snippet
	if (defined($snippet)) {
		$snippet = decodeToUTF($snippet, $$fields{'encoding'});

		# remove all newline characters and replace the <passage!> tags with \n
		$snippet =~ s/\n/ /g;	
		$snippet =~ s// /g;	
		$snippet =~ s/<passage!>/\n/g;
		$snippet =~ s/<\/passage!>/\n/g;

		# strip the HTML from the snippet
		$snippet = stripHTML($snippet);
		# trim the snippet to the proper length
		$snippet = trimSnippet($snippet);
	} else {
		$snippet = '';
	}

	return $snippet . comment($$fields{'encoding'});
}

################################################################################
# &titleXMail ($fields)
#
# Returns: $title
#  $title: subject of the e-mail from the Subject: line or
#          filename if the line is not present
################################################################################
sub titleXMail {
	my ($fields) = @_;
	my $filename = $$fields{'title_filename'};
	my $headers = $$fields{'headers'};
	$headers = decodeMailHeader($headers);
	
	my @subj = $headers =~ /^Subject: (.*)/m;

	return $subj[0] || $filename;
}

################################################################################
# &headerXMail ($fields)
#
# Returns: $header
#  $header: contains the following:
#	-sender
#	-recipient
#	-timestamp
#	-newsgroup
################################################################################
sub headerXMail {
	my ($fields) = @_;
	my $header = $$fields{'headers'};
	$header =~ s///g;
	$header =~ s/^\s*//;

	my @headers = ('Date', 'From', 'To', 'Subject', 'Bcc', 'cc', 'Newsgroups');
	if ($header =~ /From .*?\n\n/ms) {
		($header) = $header =~ /(From .*?\n\n).*$/ms;
	}

	$header = decodeMailHeader($header);
	$header =~ s/</&lt;/g;
	$header =~ s/>/&gt;/g;

	my @lines = split ('\n', $header);
	my @output;
	my $currentTag = '';
	my $table;

	$table = start_table();

	foreach (@lines) {
		if ($_ =~ /^(\S*?):/) {
			$currentTag = $1;
			my (@extract) = join ('', @output) =~ /(\S*?:)(.*)/;
			if (defined $extract[0] && defined $extract[1] && $extract[1] =~ /\S/) {
				$table .= Tr(td({-valign=>'top'}, b(ucfirst($extract[0]))) . td(highlightTerms(trimEndString($extract[1],$lengthOfHeader))));
			}
			@output = ();
		} elsif ($_ =~ /^$/) {
			last;
		}

		foreach my $i (@headers) {
			if ($currentTag =~ /^$i$/i) {
				push (@output, $_);
			}
		}
	}

	my (@extract) = join ('', @output) =~ /(\S*?:)(.*)/;
	if (defined $extract[0] && defined $extract[1] && $extract[1] =~ /\S/) {
		$table .= Tr(td({-valign=>'top'}, b(ucfirst($extract[0]))) . td(highlightTerms(trimEndString($extract[1],$lengthOfHeader))));
	}

	$table .= end_table();

	return $table;
}

################################################################################
# &contentXMail ($fields)
#
# Returns: $content
#  $content: a snippet containing the relavant passage
################################################################################
sub contentXMail {
	my ($fields) = @_;
	my $snippet = $$fields{'snippet'};
	my $headers = $$fields{'headers'};
	my $encoding = 'us-ascii';	# default according to RFC1521

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

		# remove all newline characters and replace the <passage!> tags with \n
		$snippet =~ s/\n/ /g;	
		$snippet =~ s/<passage!>/\n/g;
		$snippet =~ s/<\/passage!>/\n/g;

		if (defined $contentType[0] && $contentType[0] =~ /text\/html/i) {
			# strip HTML
			$snippet = stripHTML($snippet);
		}

		$snippet =~ s/</&lt;/g;
		$snippet =~ s/>/&gt;/g;

		# trim snippet
		$snippet = trimSnippet($snippet);
		$snippet =~ s/\n/ /g;
	}

	return $snippet;
}

################################################################################
# &contentAudio ($fields)
#
# Return: $content
#  $content: name/content pairs from the metadata of the audio file
################################################################################
sub contentAudio {
	my ($fields) = @_;
	my $snippet = $$fields{'headers'};
	my (%tags, @order);

	extractTag($snippet, \%tags, \@order);
	my $contentBlock = '';

	foreach my $name (@order) {
		if ($tags{$name} =~ /\w/) {
			my $fieldname= $name;
			$fieldname =~ tr/A-Z/a-z/;
			$contentBlock .= b(ucfirst($fieldname)) . ": " . $tags{$name} . br;
		}
	}

	return $contentBlock;
}

################################################################################
# &titleTREC ($fields)
#
# Returns: $title
#  $title: title of the HTML document from <title>..</title> tags 
#           if TREC document is HTML
#          otherwise, the TREC document number
################################################################################
sub titleTREC {
	my ($fields) = @_;
	my $filename = $$fields{'filename'};
	my $snippet = $$fields{'headers'};
	my $header = (extractHTML($snippet, "DOCHDR"))[0];
	my $docNO = (extractHTML($snippet, "DOCNO"))[0];

	$header =~ /Content-type: (.*)/i;

	if ($1 =~ /text\/html/) {
		return (extractHTML($snippet, "title"))[0] || $docNO;
	} else {
		return $docNO;
	}
}

################################################################################
# &headerTREC ($fields)
#
# Returns: $header
#  $header: contains the following:
#	-TREC document number
#	-URL of original document
################################################################################
sub headerTREC {
	my ($fields) = @_;
	my $hdr = $$fields{'headers'};
	my $docno = (extractHTML($hdr, "DOCNO"))[0];
	my $header = (extractHTML($hdr, "DOCHDR"))[0];

	my $content = b('DOCNO: ') . $docno;
	if ($header =~ /Content-type: (.*)/i) {
		my @lines = split (' ', $header);
		$content .= br . b('URL: ') . &trimString($lines[0],80);
	}
	return $content;
}

################################################################################
# &contentTREC ($fields)
#
# Returns: &content($fields)
#  &content: return value of a call to &contentHTML or &contentGeneric based
#            on the content type of the TREC document
################################################################################
sub contentTREC {
	my ($fields) = @_;
	my $hdr = $$fields{'headers'};
	my $trecHeader = (extractHTML($hdr, "DOCHDR"))[0];
	my $content = $$fields{'snippet'};

	$trecHeader =~ /Content-type: (.*)/i;

	if ($1 =~ /text\/html/) {
		return &contentHTML($fields);
	} else {
		return &contentGeneric($fields);
	}
}
################################################################################
################################################################################
##  end &processResult functions
################################################################################
################################################################################

################################################################################
################################################################################
##  &queryGetBottomFrame() Sub-functions
################################################################################
################################################################################

################################################################################
# &renderContent ($fileContent)
#  Renders $fileContent based on document type
#
# Returns: ($fileContent, $contentType, $charset)
#  $fileContent: rendered version of input
#  $contentType: content type of $fileContent
#  $charset: character set of $fileContent
################################################################################
sub renderContent {
	my ($fileContent, $filetype) = @_;
	my ($contentType, $charset);
	if (defined $filetype) {
		if ($filetype eq 'text/x-mail') {
			($fileContent, $contentType, $charset) = &renderXMail($fileContent);
		} elsif ($filetype eq 'text/html') {
			($fileContent, $contentType, $charset) = &renderHTML($fileContent);
		} elsif ($filetype eq 'audio/mpeg') {
			($fileContent, $contentType, $charset) = &renderAudio($fileContent);
		} elsif ($filetype eq 'application/pdf' || $filetype eq 'application/postscript') {
			$charset = 'LATIN1';
			$contentType = 'text/html';
			$fileContent = "<pre wrap>" . escapeHTML($fileContent) . "</pre>";
			$fileContent = table({-border=>"0", cellpadding=>"5", -cellspacing=>"5", -width=>"100%"}, Tr(td($fileContent)));
		} elsif ($filetype eq 'text/x-trec') {
			($fileContent, $contentType, $charset) = &renderTREC($fileContent);
		} else {
			$contentType = 'text/plain';
			$charset = $systemCodeset;
		}
	} else {
	}
	return ($fileContent, $contentType, $charset);
}

################################################################################
################################################################################
## start &renderContent functions
##  Functions return ($content, $contentType, $charset)
################################################################################
################################################################################

################################################################################
# &renderHTML ($content)
#
# Returns: ($content, $contentType, $charset)
#  $content: original $content
#  $contentType: 'text/html'
#  $charset: character set from META tags, or $systemCodeset
################################################################################
sub renderHTML {
	my ($content) = @_;
	my $isoEncoding = $systemCodeset;
	my @encoding = $content =~ /<meta.*?charset(.*?)>/si;
	if (defined $encoding[0]) {
		@encoding = $encoding[0] =~ /([\w-]+)/;
		$isoEncoding = $encoding[0];
	}

	return ($content, 'text/html', $isoEncoding);
}

################################################################################
# &renderAudio ($content)
#
# Returns: ($content, $contentType, $charset)
#  $content: HTML string containing formatted metadata extracted from $content
#  $contentType: 'text/html'
#  $charset: $systemCodeset
################################################################################
sub renderAudio {
	my ($content) = @_;
	my (%tags, @order);
	extractTag($content, \%tags, \@order);
	$content = '';

	foreach my $name (@order) {
		if ($tags{$name} =~ /\w/) {
			my $fieldname= $name;
			$fieldname =~ tr/A-Z/a-z/;
			$content .= b(ucfirst($fieldname)) . ": " . $tags{$name} . br;
		}
	}

	$content = start_html(-style=>{-code=>$pageStyle}, -BGCOLOR=>$pageColor) . $content . end_html();

	return ($content, 'text/html', $systemCodeset);
}

################################################################################
# &renderXMail ($content)
#
# Returns: ($content, $contentType, $charset)
#  $content: fields from the XMAIL header that are defined in @headers
#            followed by the document body converted to UTF-8
#  $contentType: 'text/html', 'text/plain' if XMAIL header cannot be extracted
#  $charset: character set from the Content-Type field in the XMAIL header
#            otherwise 'us-ascii'
################################################################################
sub renderXMail {
	my ($content) = @_;
	$content =~ s///g;
	
	my @isoEncoding = $content =~ /^Content-Type:.*?; charset\s*=\s*(.*?)(;.*)*$/m;
	my (@extract) = $content =~ /(From .*?\n\n)(.*)$/ms;
	my $header = $extract[0];
	my $body = $extract[1];
	my $contentType;
	my $charset = 'us-ascii';	# default according to RFC1521

	my @headers = ('Date', 'From', 'To', 'Subject', 'Bcc', 'cc', 'Newsgroups');

	if (defined $header) {
		if (defined ($isoEncoding[0])) {
			#$body = decodeToUTF($body, $isoEncoding[0]);
			$charset = $isoEncoding[0];
		}
		$header = decodeMailHeader($header, $charset);

		$header =~ s/</&lt;/g;
		$header =~ s/>/&gt;/g;

		my @lines = split ('\n', $header);
		my @output;
		my $currentTag = '';
		my $table;

		$table = start_table();

		foreach (@lines) {
			if ($_ =~ /^(\S*?):/) {
				$currentTag = $1;
				my (@extract) = join ('', @output) =~ /(\S*?:)(.*)/;
				if (defined $extract[0] && defined $extract[1] && $extract[1] =~ /\S/) {
					$table .= Tr(td({-valign=>'top'}, b(ucfirst($extract[0]))) . td("&nbsp;") . td($extract[1]));
				}
				@output = ();
			}

			foreach my $i (@headers) {
				if ($currentTag =~ /^$i$/i) {
					push (@output, $_);
				}
			}
		}

		my (@extract) = join ('', @output) =~ /(\S*?:)(.*)/;
		if (defined $extract[0] && defined $extract[1] && $extract[1] =~ /\S/) {
			$table .= Tr(td({-valign=>'top'}, b(ucfirst($extract[0]))) . td("&nbsp;") . td($extract[1]));
		}

		$table .= end_table();

		$body =~ s/</&lt;/g;
		$body =~ s/>/&gt;/g;
		$body = '<pre wrap>' . $body . '</pre>';

		$content = table({-border=>"0", cellpadding=>"5", -cellspacing=>"5", -width=>"100%"}, Tr(td($table . $body)));
		$content = start_html(-style=>{-code=>$pageStyle}, -BGCOLOR=>$pageColor) . $content . end_html();

		$contentType = 'text/html';
	} else {
		$contentType = 'text/plain';
	}

	return ($content, $contentType, $charset);
}

################################################################################
# &renderTREC ($content)
#
# Returns: ($content, $contentType, $charset)
#  $content: content of the TREC document
#  $contentType: 'text/html' if TREC header indicates an HTML document
#                'text/plain' otherwise
#  $charset: character set extracted from META tags in HTML document,
#            or $systemCodeset otherwise
################################################################################
sub renderTREC {
	my ($content) = @_;
	my ($header, $type, $encoding);
	if ($content =~ /<DOC>(?:.*?)<DOCNO>(.*?)<\/DOCNO>(?:.*?)<DOCHDR>(.*?)<\/DOCHDR>(.*)/s) {
		$content = $3;
		$header = $2;
		$content =~ s/<\/DOC>.*$//;
	}

	$header =~ /Content-type: (.*)/i;

	$encoding = $systemCodeset;
	if ($1 =~ /text\/html/) {
		my @meta = $content =~ /<(?:meta|META).*?charset(.*?)>/;
		if (defined $meta [0]) {
			@meta = $meta[0] =~ /([\w-]+)/;
			$encoding = $meta[0];
		}
		$type = 'text/html';
	} else {
		$type = 'text/plain';
	}
	return ($content, $type, $encoding);
}

################################################################################
################################################################################
## end &renderContent functions
################################################################################
################################################################################

################################################################################
################################################################################
##  main Sub-Functions
################################################################################
################################################################################

################################################################################
# &bodySearchBox ()
#  Generates HTML to display search box, using parameters passed to the script
#   to populate values
#
# Returns: $searchBox
#  $searchBox: HTML string of search box
################################################################################
sub bodySearchBox {
	my $searchBox;

	my $logo = a({-href=>$absURL}, img({-src=>"wumpus_logo.gif", -border=>"0"}));
	my $copyright = font({-size=>-1}, $wumpusCopyright);
	my $query_submit_button = submit(-label=>"Submit");

	my $searchfield = textfield(
		-name=>'sf',
		-size=>56,
		-maxlength=>256
	);
	
	my $filetype = radio_group(
		-name=>'searchtype',
		-values=>['everything', 'files', 'email', 'office'],
		-default=>'everything',
		-labels=>\%searchTypeToDescription
	);

	my $advancedQuery= new CGI();
	$advancedQuery->delete('tframe', 'bframe');
	$advancedQuery->param('advanced', '');
	my $advancedURL = $advancedQuery->url({-absolute=>1, -query=>1});

	$searchBox .= start_form(-method=>$formMethod, -action=>$absURL, -name=>"form");
	$searchBox .= table(
		td([
			table([Tr([
				td({-valign=>"top", -align=>"center"}, $logo),
				td($copyright)
				])
			]),
			table(
				Tr([
					td(["Search query:", $searchfield, $query_submit_button]),
					td({-align=>"center"}, [font({-size=>"-1"},a({-href=>"logout.pl"}, "Logout")), font({-size=>"-1"}, $filetype), font({-size=>"-1"}, a({-href=>$advancedURL}, "Advanced"))])
				])
			)
		])
	);
	$searchBox .= end_form;

	return $searchBox;
}

################################################################################
# &bodyAdvancedSearchBox ()
#  Generates HTML to display advanced search box, using parameters passed to the script
#   to populate values
#
# Returns: $searchBox
#  $searchBox: HTML string of advanced search box
################################################################################
sub bodyAdvancedSearchBox {
	my $searchBox;

	my $logo = a({-href=>$absURL}, img({-src=>"wumpus_logo.gif", -border=>"0"}));
	my $copyright = font({-size=>-1}, $wumpusCopyright);
	my $query_submit_button = submit(-label=>"Submit");

	my $searchfield = textfield(
		-name=>'sf',
		-size=>72,
		-maxlength=>256
	);

	my $fnfield = textfield(
		-name=>'sfn',
		-size=>40,
		-maxlength=>40
	);

	my $fslessfield = textfield(
		-name=>'sfsl',
		-size=>16,
		-maxlength=>16
	);

	my $fsgreaterfield = textfield(
		-name=>'sfsg',
		-size=>16,
		-maxlength=>16
	);

	my $filetype = scrolling_list(
		-name=>'searchtype[]',
		-values=>[@fileType],
		-size=>7,
		-multiple=>'true',
		-labels=>\%fileTypeToDescription
	);

	my $standardQuery= new CGI();
	$standardQuery->delete('advanced', 'tframe', 'bframe');
	my $standardURL = $standardQuery->url({-absolute=>1, -query=>1});

	$searchBox .= start_form(-method=>$formMethod, -action=>$absURL);
	$searchBox .= hidden(-name=>'advanced');
	$searchBox .= table(
		td({-valign=>"top", -align=>'center'}, [
			table([
				td($logo),
				td($copyright)
			]),
			table({-cellspacing=>2, -cellpadding=>2},
				Tr([
					td("Search&nbsp;query:") .
						td({-colspan=>2}, $searchfield) .
						td($query_submit_button),
					td({-valign=>'top', -align=>'center'}, font({-size=>"-1"}, a({-href=>"logout.pl"}, "Logout"))) . 
						td({-valign=>'top', -align=>'left'},
							b("File name") . " (wild card expression)" . br . $fnfield . br .
							b("File size") . br . $fslessfield . " - " . $fsgreaterfield . br) .
						td({-valign=>'top', -align=>'center'}, font({-size=>"-1"}, $filetype)) .
						td({-valign=>'top', -align=>'center'}, font({-size=>"-1"}, a({-href=>$standardURL}, "Standard"))
					)
				])
			)
		])
	);
	$searchBox .= end_form;

	return $searchBox;
}

################################################################################
# &bodyInformationBar ($infobarTitle, $infobarContent)
#  Generates header bar from the two parameters
#
# Returns: $infobar
#  $infobar: HTML string of header bar
################################################################################
sub bodyInformationBar {
	my ($infobarTitle, $infobarContent) = @_;
	my $infobar;

	$infobar .= table({-width=>"100%", -border=>0, -cellpadding=>0, -cellspacing=>0},
		Tr([
			td(
				{-BGCOLOR=>"#4070D0"}, [img({-width=>1, -height=>1, -alt=>""})]
			)
		])
	);

	#$infobar .= table({-width=>"100%", -BGCOLOR=>"#E5ECF9", -border=>"0", -cellpadding=>"0", -cellspacing=>"0"},
	$infobar .= table({-width=>"100%", -border=>"0", -cellpadding=>"0", -cellspacing=>"0"},
		Tr(
			td(
				{-BGCOLOR=>"#E0F0FF", -nowrap=>'true', -width=>"10"}, ''
			),
			td(
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
	return $infobar;
}

################################################################################
################################################################################
##  Miscellaenous Sub-Functions
################################################################################
################################################################################

################################################################################
# &highlightTerms ( $string_to_highlight )
#  Highligh all query terms in $string
#
# Returns: $string
#  $string: $string_to_highlight with query terms tagged with HTML highlighting
################################################################################
sub highlightTerms {
	my $currentTime = [gettimeofday];

	# hash of stemmed strings
	our %stemmed;

	# input string and query terms
	my ($string_to_highlight) = @_;
	my $terms_to_highlight = (extractSearchTerms())[2];
	my $string;

	my $open = $highlightOpen;
	my $close = $highlightClose;

	# query terms to highlight
	my %query_terms;

	# populate hash with query terms and stemming if applicable
	foreach my $term ($terms_to_highlight =~ /"(.*?)"/g) {
		# remove whitespace characters from the term
		$term =~ s/\p{InWWS_stem}/ /g;

		# tokenize $term
		foreach my $subterm (split ' ', $term) {
			if ($subterm =~ /\$/) {
				# found stemming '$' in subterm
				$subterm =~ s/\$/ /g;
				if (!(defined $stemmed{lc $subterm})) {
					$stemmed{lc $subterm} = (split("\n", $connection->wumpus_stem(lc $subterm)))[0];
					# check if return value is [unstemmable]
					if ($stemmed{lc $subterm} eq '[unstemmable]') {
						$stemmed{lc $subterm} = '';
					}
				}
				$query_terms{$subterm} = $stemmed{lc $subterm};
			} else {
				$query_terms{$subterm} = '';
			}
		}
	}

	# tokenize input string with 
	my @array = split /(\p{InWWS}| |\n|)/o, $string_to_highlight;

	foreach my $term (keys %query_terms) {
		my ($term_stemmed, $term_substr);
		if ($query_terms{$term} ne '') {
			$term_stemmed = $query_terms{$term};
			$term_substr = substr($term_stemmed, 0, 3);
		} else {
			$term_stemmed = '';
		}

		foreach my $i (@array) {
			if ($term_stemmed ne '') {
				# check if they are similar
				if ($term_substr eq substr(lc $i, 0, 3)) {
					# stem the term
					if (defined $stemmed{lc $i}) {
						if ($stemmed{lc $i} =~ /^$term_stemmed$/i) {
							$i = $open . $i . $close;
						}
					} else {
						$stemmed{lc $i} = (split("\n", $connection->wumpus_stem(lc $i)))[0];
						if ($stemmed{lc $i} =~ /^$term_stemmed$/i) {
							$i = $open . $i . $close;
						}
					}
				}
			} else {
				$i =~ s/\b($term)\b/$open$1$close/i;
			}
		}
	}
	$string = join ('', @array);

	# extend tags over 'whitespace'
	$string =~ s/$close((?:\p{InWWS}| )*)$open/$1/g;

	$highlightTime += tv_interval($currentTime);
	return $string;
}

################################################################################
# &decodeMailHeader ($string_to_decode, $type)
#  Converts $string_to_decode to $type
#
# Returns: $string
#  $string: converted string
################################################################################
sub decodeMailHeader {
	my ($string_to_decode, $type) = @_;
	my $string;

	Text::Iconv->raise_error(1);

	eval {
		foreach my $i (decode_mimewords($string_to_decode)) {
			$string .= Text::Iconv->new($i->[1] || $systemCodeset, $type || 'UTF-8')->convert($i->[0]);
		}
	};

	# catch exception
	if ($@) {
		return $string_to_decode;
	} else {
		return $string;
	}
}

################################################################################
# &decodeToUTF ($string_to_decode, $type)
#  Converts $string_to_decode to UTF-8 from $type
#
# Returns: $string
#  $string: converted string
################################################################################
sub decodeToUTF {
	my ($string_to_decode, $type) = @_;
	my $string;

	Text::Iconv->raise_error(1);

	eval {
		$string = Text::Iconv->new($type || $systemCodeset, 'UTF-8')->convert($string_to_decode);
	};

	# catch exception
	if ($@) {
		return $string_to_decode;
	} else {
		return $string;
	}
}

################################################################################
# &stripHTML ($string)
#  Extracts alternate text (alt="") from tags then removes all HTML tags and &nbsp; tags
#   from $string
#
# Returns: $string
#  $string: HTML stripped string
################################################################################
sub stripHTML {
	my ($string) = @_;

	# remove comments <!-- -->
	$string =~ s/<!--.*?-->//gs;

	# remove scripts
	$string =~ s/<script.*?\/script>//gs;
	$string =~ s/^.*?\/script>//gs;
	$string =~ s/<script.*?$//gs;

	# remove styles
	$string =~ s/<style.*?\/style>//gs;
	$string =~ s/^.*?\/style>//gs;
	$string =~ s/<style.*?$//gs;

	# look for 'alt'
	$string =~ s/alt="(.*?)"/>$1</gs;

	# simply html stripping: remove all tags, remove extraneous newline characters
	$string =~ s/<.*?>/ /g;

	# tags that cross newlines delimit the snippet
	$string =~ s/<.*?\n.*?>/\n/g;
	$string =~ s/<.*?\n.*?\n.*?>/\n\n/g;

	# partial tags at beginning and end of snippet
	$string =~ s/^.*?>//;
	$string =~ s/<.*?$//;

	# remove &nbsp; tags
	$string =~ s/&nbsp;//g;

	return $string;
}

################################################################################
# &timeToDate ($time)
# &timeToTimeDate ($time)
#  Converts $time from seconds since epoch to string form
# Returns: $string
#  $string: $time in string form
################################################################################
sub timeToDate {
	my ($time) = @_;
	return strftime "%e %b %Y", gmtime($time);
}

sub timeToTimeDate {
	my ($time) = @_;
	return strftime "%e %b %Y %l:%M:%S %p" , gmtime($time);
}

################################################################################
# &sizeToString ($size)
#  Converts filesize $size to human-readable format (i.e. bytes, KB, MB, GB)
#
# Returns:
#  $string: $size in human-readable format
################################################################################
sub sizeToString {
	my ($size) = @_;
	my $string;
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
		$string = (sprintf "%d", $size) . " " . $exp_name[$exp];
	} else {
		$string = (sprintf "%0.3f", $size) . " " . $exp_name[$exp];
	}
	return $string;
}

################################################################################
# &extractTag ($string, \%hash, \@order)
#  Extracts content from XML tags, storing the content in the hash keyed by the tag
#  Extracts the order of the XML tags, storing the names in the array
################################################################################
sub extractTag {
	my ($string, $hash, $order) = @_;
	%$hash = $string =~ /<(.*?)>(.*?)<\/\1>/gm;
	@$order = $string =~ /<(.*?)>.*?<\/\1>/gm;
}

################################################################################
# &trimFilename ($filename)
#  Generates a pathname of length less than $lengthOfFilename
#   consisting of two components from the back of the path and
#   as many from the front of the path
#
# Returns: $filename
#  $filename: trimmed filename
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

################################################################################
# &trimString ($string, $length)
#  Replaces characters from the center of $string with '.....'
#   if the string exceeeds $length
# Returns: $string
#  $string: trimmed string
################################################################################
sub trimString {
	my ($string, $length) = @_;
	if (length($string) > $length) {
		return substr($string, 0, int ($length/2)) . '.....' . substr($string, int -($length/2), int ($length/2));
	} else {
		return $string;
	}
}

################################################################################
# &trimEndString ($string, $length)
#  Replaces characters from the end of $string with '.....'
#   if the string exceeeds $length
# Returns: $string
#  $string: trimmed string
################################################################################
sub trimEndString {
	my ($string, $length) = @_;
	if (length($string) > $length) {
		return substr($string, 0, $length) . '...';
	} else {
		return $string;
	}
}

################################################################################
# &extractWumpusTag ($search_string, $tag)
#  Extracts from $search_string contents between $tag
#
# Returns: $string
#  $string: content between $tag
################################################################################
sub extractWumpusTag {
	my ($search_string, $tag) = @_;

	return $search_string =~ /<$tag!>(.*?)<\/$tag!>/gs;
}

################################################################################
# &extractHTML ($search_string, $tag)
#  Extracts from $search_string contents between $tag
#
# Returns: $string
#  $string: content between $tag
################################################################################
sub extractHTML {
	my ($search_string, $tag) = @_;

	return $search_string =~ /<$tag>(.*?)<\/$tag>/gsi;
}

# end of file
