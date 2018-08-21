#!/usr/bin/perl -w
# SPDX-License-Identifier: AGPL-3.0-only

use strict;
use DBI;
use POSIX qw(locale_h);

# global database variables
my ($dbh, $query, $sth, $par, $udh);

sub readconfig($) {
	my ($fn) = @_;
	my %options;

	open(CFG, $fn) or die("unable to open ".$fn." config file");
	while (<CFG>) {
		if ($_ =~ /^\s*[#!]/) {
			next;
		}
		if ($_ =~ /^\s*(\S+)\s*=\s*([^\r]+)\r?$/) {
			my $idx = $1;
			my $val = $2;
			chomp($val);
			$val =~ s/\s+$//;
			$options{$idx} = $val;
		}
	}
	close(CFG);
	return %options;
}

sub databasename($$) {
	my ($id, $type) = @_;
	my @row;
	my $tag = 0x3001;	# PR_DISPLAY_NAME
	if ($type == 5) {
		$tag = 0x37;	# PR_SUBJECT
	}
	$sth->execute($tag, $id);
	@row = $sth->fetchrow_array();
	if (@row && defined($row[0])) {
		return $row[0];
	} else {
		if ($type == 1) {
			return "store";
		} else {
			return "$id";
		}
	}
}

sub databasename_tree($$) {
	my ($id, $type) = @_;
	my @tree;
	my @row = ($id,$type);

	while (@row) {
		push(@tree, databasename($id, $type));
		$par->execute($id);
		@row = $par->fetchrow_array();
		if (@row) {
			if (defined($row[0])) {
				$id = $row[0];
				$type = $row[1];
			} else {
				last;
			}
		}
	}
	return "'".join("\\", reverse(@tree))."'";
}

sub mapitype($) {
	my ($type) = @_;
	# only store, folder and message will actually happen
	if ($type eq 1) {
		return "'store'";
	} elsif ($type eq 2) {
		return "'addrbook'";
	} elsif ($type eq 3) {
		return "'folder'";
	} elsif ($type eq 4) {
		return "'abcont'";
	} elsif ($type eq 5) {
		return "'message'";
	} elsif ($type eq 6) {
		return "'mailuser'";
	} elsif ($type eq 7) {
		return "'attach'";
	} elsif ($type eq 8) {
		return "'distlist'";
	} elsif ($type eq 9) {
		return "'profsect'";
	} elsif ($type eq 10) {
		return "'status'";
	} elsif ($type eq 11) {
		return "'session'";
	} elsif ($type eq 12) {
		return "'forminfo'";
	}
	return "'unknown'";
}

sub parse_entry($) {
	my ($input) = @_;
	my $output = $input;
	my %values = ();

	if ($input =~ m/access (\S+) (.*)/) {
		my @options = split(/ /,$2);
		foreach(@options) {
			my @c = split(/=/,$_);
			$values{$c[0]} = $c[1];
		}

		if (keys(%values) > 0) {
			$values{objectid} = databasename_tree($values{objectid}, $values{type});
			$values{type} = mapitype($values{type});

			@options = ();
			foreach(keys(%values)) {
				push(@options, $_."=".$values{$_});
			}
		}

		$output = "access $1 ".join(" ", @options);
	}

	return $output;
}


#### main ####
setlocale(LC_CTYPE, "");

my $servercfg = $ARGV[0];
$servercfg = "/etc/kopano/server.cfg" if (!defined($servercfg));
my %serveropt = readconfig($servercfg);

$dbh = DBI->connect("dbi:mysql:database=".$serveropt{mysql_database}.";host=".$serveropt{mysql_host}, $serveropt{mysql_user}, $serveropt{mysql_password})
	or die "Database error: ".$DBI::errstr;
$dbh->do("set character_set_client = 'utf8'")
	or die "Database error: ".$DBI::errstr;
$dbh->do("set character_set_connection = 'utf8'")
	or die "Database error: ".$DBI::errstr;
$dbh->do("set character_set_results = 'utf8'")
	or die "Database error: ".$DBI::errstr;

$query = "SELECT val_string FROM properties WHERE type=30 and tag=? and hierarchyid=?";
$sth = $dbh->prepare($query)
	or die "Database error: ".$DBI::errstr;
$query = "select ph.id as pid, ph.type as ptype from hierarchy as ph join hierarchy on ph.id=hierarchy.parent where hierarchy.id=?";
$par = $dbh->prepare($query)
	or die "Database error: ".$DBI::errstr;

while (<STDIN>) {
# syslog
# Feb 28 11:17:21 <hostname> kopano-server[5307]: server startup by user uid=1000
# Feb 28 17:53:35 <hostname> kopano-server[5311]: authenticate ok user='jdoe' from='file:///var/run/kopano/server.sock' method='Pipe socket' program='kopano-dagent'
# Feb 28 13:07:04 <hostname> kopano-server[5311]: access allowed objectid=991 type=3 ownername='constant' username='jdoe' rights='view'
# ...

# logfile
# di 01 mrt 2011 09:48:29 CET: server startup by user uid=1000
	my $fixup;

	if ($_ =~ m/(.*) (kopano-server\[\d+\]): (.*)/) { # syslog
		$fixup = parse_entry($3);
		print "$1 $2: $fixup\n";
	} elsif ($_ =~ /(.*): (.*)/) { # too generic?
		$fixup = parse_entry($2);
		print "$1: $fixup\n";
	}
}
