package WumpusConnection;
require Exporter;
$| = 1;
use strict;

our @ISA	= qw(Exporter);
our @EXPORT	= qw(
	new wumpus_connect wumpus_search stem_stat wumpus_fileinfo wumpus_stem wumpus_summary
	wumpus_filestat wumpus_get wumpus_getfile wumpus_getaudio connected InWWS InWWS_stem);

use IO::Socket;

sub new {
	my $invocant = shift;
	my $class = ref($invocant);
	my $self = { 
		hostname => "localhost",
		hostport => "1234",
		sock => undef,
		stem => 0,
		@_
	};
	if (defined $ENV{WUMPUS_PORT}) {
		$self->{hostport} = $ENV{WUMPUS_PORT};
	}

	# connect to the server
	$self->{sock} = IO::Socket::INET->new(PeerAddr => $self->{hostname}, PeerPort => $self->{hostport});
	my $socket = $self->{sock};
	my $line = <$socket>;

	bless($self, $class);	
	if (defined($self->{sock})) {
		$self->{connected} = 1;
	} else {
		$self->{connected} = 0;
	}
	return $self;
}

sub wumpus_connect {
	my ($self, $username, $password) = @_;
	my $socket = $self->{sock};

	print $socket "\@login $username $password\n";
	my $line = <$socket>;
	if ($line eq "\@0-Authenticated.\n") {
		return 1;
	}
}

sub wumpus_search {
	# Stops reading when input line is of the following form: @[status]. (## ms)
	my ($self, $search_string, $start, $end) = @_;
	my $socket = $self->{sock};

	print $socket "\@desktop[count=2000][start=$start][end=$end] $search_string\n";

	my $time;
	my $line;
	my @results;
	my @status;
	while ($line = <$socket>) {
		if (@status = $line =~ /^@(.*?). \((.*)\)\n/) {
			$time = (split(' ', $status[1]))[0];
			last;
		} else {
			push @results, $line;
		}
	}
	return (join ('', @results), $time/1000.0);
}

sub wumpus_filestat {
	my ($self) = @_;
	my $socket = $self->{sock};

	print $socket "\@filestats\n";

	my $line;
	my @results;
	my @status;

	while ($line = <$socket>) {
		if (@status = $line =~ /^@(.*?). \((.*)\)\n/) {
			last;
		} else {
			push @results, $line;
		}
	}

	return (join ('', @results));
}

sub wumpus_summary {
	my ($self) = @_;
	my $socket = $self->{sock};

	print $socket "\@summary\n";

	my $line;
	my @results;
	my @status;

	while ($line = <$socket>) {
		if (@status = $line =~ /^@(.*?). \((.*)\)\n/) {
			last;
		} else {
			push @results, $line;
		}
	}

	return (join ('', @results));
}

sub wumpus_fileinfo {
	my ($self, $index) = @_;
	my $socket = $self->{sock};

	print $socket "\@fileinfo $index\n";

	my $line = <$socket>;
	my $status;
	if ($line !~ /^@/) {
		$status = <$socket>;
		my @parts = $line =~ /^(.*?) (.*?)\n$/;
		return (@parts);
	} else {
		return (undef, undef);
	}
}

sub wumpus_stem {
	my ($self, $string) = @_;
	my $socket = $self->{sock};
	
	print $socket "\@stem $string\n";

	my $line = <$socket>;
	my $status = <$socket>;
	$self->{stem}++;
	return $line;
}

sub stem_stat {
	my ($self) = @_;
	return $self->{stem};
}

sub wumpus_get {
	my ($self, $start, $end) = @_;
	my $socket = $self->{sock};

	print $socket "\@get $start $end\n";

	my $time;
	my $line;
	my @results;
	my @status;
	while ($line = <$socket>) {
		if (@status = $line =~ /^@(.*?). \((.*)\)\n/) {
			$time = (split(' ', $status[1]))[0];
			last;
		} else {
			push @results, $line;
		}
	}
	return (join ('', @results), $time/1000.0);
}

sub wumpus_getfile {
	use bytes;
	my ($self, $filename) = @_;
	my ($byteCount, $data, $type, $charset);
	my $socket = $self->{sock};

	print $socket "\@getfile $filename\n";

	my $line = <$socket>;
	if ($line =~ /^\@[0-9]-(.*)/) {
		# No data coming
		return ($line);
	} else {
		$type = $line;
		while ($line = <$socket>) {
			if ($line =~/^[0-9]*\n$/) {
				last;
			}
		}
		$byteCount = $line;

		$socket->read($data, $byteCount);

		$charset = ($type =~ /charset=(.*?)/)[0];
		$type = (split (';', $type))[0];
		return ($data, $type, $charset);
	}
}

sub wumpus_getaudio {
	use bytes;
	my ($self, $filename) = @_;
	my ($byteCount, $data, $type, $charset);
	my $socket = $self->{sock};

	print $socket "\@getaudio $filename\n";

	my $line = <$socket>;
	if ($line =~ /^\@[0-9]-(.*)/) {
		# No data coming
		return ($line);
	} else {
		$type = $line;
		while ($line = <$socket>) {
			if ($line =~/^[0-9]*\n$/) {
				last;
			}
		}
		$byteCount = $line;

		$socket->read($data, $byteCount);

		$charset = ($type =~ /charset=(.*?)/)[0];
		$type = (split (';', $type))[0];
		return ($data, $type, $charset);
	}
}

sub InWWS {
	return <<END;
21
22
23
24
25
27
28
29
2b
2c
2d
2e
2f
3a
3d
3f
5b
5d
5e
5f
7b
7c
7d
7e
a7
b0
END
}

sub InWWS_stem {
	return <<END;
21
22
23
25
27
28
29
2b
2c
2d
2e
2f
3a
3d
3f
5b
5d
5e
5f
7b
7c
7d
7e
a7
b0
END
}

# end of file
