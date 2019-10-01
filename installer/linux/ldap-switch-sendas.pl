#!/usr/bin/perl -w
# SPDX-License-Identifier: AGPL-3.0-only
use strict;
use Net::LDAP;
use MIME::Base64;
use Data::Dumper;

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


my $servercfg = $ARGV[0];
$servercfg = "/etc/kopano/server.cfg" if (!defined($servercfg));

my %serveropt = readconfig($servercfg);

if (!defined($serveropt{user_plugin})) {
	print "First argument must be the server configuration file\n";
	exit(0);
}
if ($serveropt{user_plugin} ne "ldap" && $serveropt{user_plugin} ne "ldapms") {
	print "You only can change the unique attribute for the ldap user plugin, found: '".$serveropt{user_plugin}."'\n";
	exit(0);
}

my %ldapopt = readconfig($serveropt{user_plugin_config});

if (!defined($ldapopt{ldap_sendas_attribute_type}) && defined($ldapopt{ldap_user_sendas_attribute_type})) { 
	$ldapopt{ldap_sendas_attribute_type} = $ldapopt{ldap_user_sendas_attribute_type};
}

if (!defined($ldapopt{ldap_sendas_relation_attribute}) && defined($ldapopt{ldap_user_sendas_relation_attribute})) {
	$ldapopt{ldap_sendas_relation_attribute} = $ldapopt{ldap_user_sendas_relation_attribute};
}

if (!defined($ldapopt{ldap_sendas_attribute}) && defined($ldapopt{ldap_user_sendas_attribute})) {
	$ldapopt{ldap_sendas_attribute} = $ldapopt{ldap_user_sendas_attribute};
}

# sanity check
if (!defined($ldapopt{ldap_sendas_attribute}) || !defined($ldapopt{ldap_sendas_attribute_type}) || !defined($ldapopt{ldap_sendas_relation_attribute})) {
	print "Some of the required attributes are not set.\nPlease check the following values (or alias config names):\n";
	if (!defined($ldapopt{ldap_sendas_attribute})) {
		print "ldap_sendas_attribute\n";
	}
	if (!defined($ldapopt{ldap_sendas_attribute_type})) {
		print "ldap_sendas_attribute_type\n";
	}
	if (!defined($ldapopt{ldap_sendas_relation_attribute})) {
		print "ldap_sendas_relation_attribute\n";
	}
	exit(0);
}

my ($ldap_search_base, $ldap_search_filter);

$ldap_search_base = $ldapopt{ldap_search_base};
$ldap_search_filter = "(&(".$ldapopt{ldap_object_type_attribute}."=".$ldapopt{ldap_user_type_attribute_value}.")".$ldapopt{ldap_user_search_filter}.")";
$ldapopt{ldap_user_scope} = "sub";

my $ldapuri = $ldapopt{ldap_protocol}."://".$ldapopt{ldap_host}.":".$ldapopt{ldap_port};
my $ldap = Net::LDAP->new($ldapuri) or die("LDAP connection failed");
my $msg = $ldap->bind($ldapopt{ldap_bind_user}, password => $ldapopt{ldap_bind_passwd}) or die ("LDAP bind failed");
$msg->code && die $msg->error;

my (%u2dn, %dn2u);
my $match = "dn";
if ($ldapopt{ldap_sendas_attribute_type} ne "dn") {
	print "Looking up all users...\n";
	if (defined($ldapopt{ldap_sendas_relation_attribute}) && $ldapopt{ldap_sendas_relation_attribute} ne "") {
		$match = $ldapopt{ldap_sendas_relation_attribute};
	} else {
		$match = $ldapopt{ldap_user_unique_attribute};
	}
	$match = lc($match);
	$msg = $ldap->search(base => $ldap_search_base, filter => $ldap_search_filter, scope => $ldapopt{ldap_user_scope}, attrs => [ $match ]);
	$msg->code && die $msg->error;
	my $users = $msg->as_struct();
	foreach (keys %$users) {
		my $dn = $_;
		my $value = $users->{$_}->{$match}[0];
		
		$u2dn{$value} = $dn;
		$dn2u{$dn} = $value;
	}
}

print "Finding send-as settings...\n";
$msg = $ldap->search(base => $ldap_search_base, filter => $ldap_search_filter, scope => $ldapopt{ldap_user_scope},
					 attrs => [$ldapopt{ldap_sendas_attribute}]);
$msg->code && die $msg->error;

my $users = $msg->as_struct();
my $attrUnique = lc($ldapopt{ldap_user_unique_attribute});
my $attrName = lc($ldapopt{ldap_loginname_attribute});
my $attrSendas = lc($ldapopt{ldap_sendas_attribute});

my %modify;
my %remove;

foreach (keys %$users) {
	my $dn = $_;
	my $valref = $$users{$_};
	my $unique = $valref->{$attrUnique}[0];
	my $username = $valref->{$attrName}[0];
	my $sendas = $valref->{$attrSendas};

	if (!defined($sendas)) {
		next;
	}

	foreach (@${sendas}) {
		print "remove $_ from $dn\n";
		push @{$remove{$dn}}, $_;

		if ($match ne "dn") {
			print "add ".$dn2u{$dn}." to ".$u2dn{$_}."\n";
			push @{$modify{$u2dn{$_}}}, $dn2u{$dn};
		} else {
			print "add $dn to $_\n";
			push @{$modify{$_}}, $dn;
		}
	}
}

print "\nApply above changes to ldap? [y] ";
my $ans = <STDIN>;
chomp($ans);
if (! ($ans eq "" || $ans =~ m/y.*/i)) {
	exit(0);
}

print "Removing old send-as settings...\n";
foreach (keys %remove) {
	my $dn = $_;

	$msg = $ldap->modify($dn, delete => [ $attrSendas ] );
	$msg->code && print $msg->error." for ".$dn."\n";
}

print "Setting new send-as settings...\n";
foreach (keys %modify) {
	my $dn = $_;

	$msg = $ldap->modify($dn, replace => { $attrSendas => [ @{$modify{$dn}} ] });
	$msg->code && print $msg->error." for ".$dn."\n";
}

$ldap->unbind();
