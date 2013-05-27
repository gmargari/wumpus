use File::Find;
use Cwd;

# destination -- should get from http.rc
my $BASE = getcwd();
my $DEST = $BASE . "/www";

# determine if Text::Iconv is installed
`perl test_iconv.pl >& /dev/null`;

if ($? == 0) {
	# installed remove files if they exist
	`rm $DEST/Text/Iconv.pm`;
	`rm $DEST/auto/Text/Iconv/Iconv.so`;
	`rm $DEST/auto/Text/Iconv/Iconv.bs`;
	`rm $DEST/auto/Text/Iconv/autosplit.ix`;
	exit;
}

if (-e "$DEST/Text/Iconv.pm") {
	exit;
}

# make && install
`tar zxf Text-Iconv-1.4.tar.gz`;
chdir('Text-Iconv-1.4');
`perl Makefile.PL && make`;

# make directories
`mkdir $DEST $DEST/Text $DEST/auto $DEST/auto/Text $DEST/auto/Text/Iconv >& /dev/null`;

# copy wanted files
find({wanted => \&copy, no_chdir => 1}, ("."));

# remove source
chdir($BASE);
`rm -rf Text-Iconv-1.4`;

# copy function
sub copy {
	my $filename = $_;
	if ($filename =~ /Iconv.pm$/) {
		`cp $File::Find::name $DEST/Text/Iconv.pm`;
	} elsif ($filename =~ /Iconv.so$/) {
		`cp $File::Find::name $DEST/auto/Text/Iconv/Iconv.so`;
	} elsif ($filename =~ /Iconv.bs$/) {
		`cp $File::Find::name $DEST/auto/Text/Iconv/Iconv.bs`;
	} elsif ($filename =~ /autosplit.ix$/) {
		`cp $File::Find::name $DEST/auto/Text/Iconv/autosplit.ix`;
	}
}
