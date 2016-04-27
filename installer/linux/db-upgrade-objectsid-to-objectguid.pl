#!/usr/bin/perl -w
use strict;
use DBI;
use Net::LDAP;
use MIME::Base64;

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

sub decode_contact_entryid($) {
	my ($entryid) = @_;
	return unpack("LB128LLLZ*", $entryid); # padding is lost
}

sub encode_contact_entryid(@) {
	my (@values) = @_;
	return pack("LB128LLLZ*CCC", @values, 0, 0, 0); # re-add padding!
}

my $servercfg = $ARGV[0];
$servercfg = "/etc/kopano/server.cfg" if (!defined($servercfg));

my %serveropt = readconfig($servercfg);

if (!defined($serveropt{user_plugin})) {
	print "First argument must be the server configuration file\n";
	exit(0);
}
if ($serveropt{user_plugin} ne "ldap") {
	print "You only can change the unique attribute for the ldap user plugin, found: '".$serveropt{user_plugin}."'\n";
	exit(0);
}

my ($dbh, $sth);
$dbh = DBI->connect("dbi:mysql:database=".$serveropt{mysql_database}.";host=".$serveropt{mysql_host}, $serveropt{mysql_user}, $serveropt{mysql_password})
	or die $DBI::errstr;

my %ldapopt = readconfig($serveropt{user_plugin_config});
my ($ldap_search_base, $ldap_search_filter);

if (!defined($ldapopt{ldap_search_base})) {
	print "Using pre 6.40 ldap config\n";
	$ldap_search_base = $ldapopt{ldap_user_search_base};
	$ldap_search_filter = $ldapopt{ldap_user_search_filter};
} else {
	print "Using post 6.40 ldap config\n";
	$ldap_search_base = $ldapopt{ldap_search_base};
	$ldap_search_filter = "(&(".$ldapopt{ldap_object_type_attribute}."=".$ldapopt{ldap_user_type_attribute_value}.")".$ldapopt{ldap_user_search_filter}.")";
	$ldapopt{ldap_user_scope} = "sub";
}

my $ldapuri = $ldapopt{ldap_protocol}."://".$ldapopt{ldap_host}.":".$ldapopt{ldap_port};
my $ldap = Net::LDAP->new($ldapuri) or die("LDAP connection failed");
my $msg = $ldap->bind($ldapopt{ldap_bind_user}, password => $ldapopt{ldap_bind_passwd}) or die ("LDAP bind failed");
$msg->code && die $msg->error;

$msg = $ldap->search(base => $ldap_search_base, filter => $ldap_search_filter, scope => $ldapopt{ldap_user_scope}, attrs => ["objectSid", "objectGuid"]);
$msg->code && die $msg->error;

$dbh->begin_work();
print "Converting users table\n";

my %extmap;
my $users = $msg->as_struct();
$dbh->{AutoCommit} = 0;
my $query = "UPDATE users SET externid=? WHERE externid=?";
$sth = $dbh->prepare($query)
	or die $DBI::errstr;
foreach (keys %$users) {
	my $valref = $$users{$_};
	my $objectSid = @$valref{objectsid}; # NOTE: all lowercase!
	my $objectGuid = @$valref{objectguid}; # NOTE: all lowercase!

	$extmap{@$objectSid[0]} = @$objectGuid[0];

	my $ra = $sth->execute(@$objectGuid[0], @$objectSid[0]);
	if ($ra == 0) {
		print "No user updated for DN '".$_."'\n";
	} elsif ($ra > 1) {
		print "Too many users match: ".@$objectSid[0]."\n"; # binary, will not print nicely.
		$dbh->rollback();
		die ($DBI::stderr);
	}
}

print "Done converting users table\n\n";
$dbh->commit();
$ldap->unbind();

# upgrade most recipient entry id's in database
my ($tag, $upd, $udh, $upd_t, $udh_t, $i);
my @row;
# tags:
# PR_RECEIVED_BY_ENTRYID, PR_SENT_REPRESENTING_ENTRYID, PR_RCVD_REPRESENTING_ENTRYID, PR_READ_RECEIPT_ENTRYID,
# PR_ORIGINAL_AUTHOR_ENTRYID, PR_ORIGINAL_SENDER_ENTRYID, PR_ORIGINAL_SENT_REPRESENTING_ENTRYID, PR_SENDER_ENTRYID,
# PR_LAST_MODIFIER_ENTRYID, PR_RECIPIENT_ENTRYID, PR_EC_CONTACT_ENTRYID
my @tags = (0x003F, 0x0041, 0x0043, 0x0046,
			0x004C, 0x005B, 0x005E, 0x0C19,
			0x3FFB, 0x5FF7, 0x6710);
$i = 1;
foreach $tag (@tags) {
	$dbh->begin_work();

	print "Converting properties table step $i of ".scalar(@tags)."\n";

	# join store so we use the index, only return addressbook entries V1
	$query = "SELECT parent.id,hierarchyid,val_binary FROM properties JOIN hierarchy ON properties.hierarchyid=hierarchy.id LEFT JOIN hierarchy AS parent ON hierarchy.parent=parent.id AND parent.type=3 WHERE properties.type=0x0102 AND tag=? AND substr(val_binary,1,24) = 0x00000000AC21A95040D3EE48B319FBA75330442501000000";
	$sth = $dbh->prepare($query)
		or die $DBI::errstr;

	$upd = "UPDATE properties SET val_binary=? WHERE hierarchyid=? and type=0x0102 and tag=?";
	$udh = $dbh->prepare($upd)
		or die $DBI::errstr;

	$upd_t = "UPDATE tproperties SET val_binary=? WHERE folderid=? and hierarchyid=? and type=0x0102 and tag=?";
	$udh_t = $dbh->prepare($upd_t)
		or die $DBI::errstr;

	$sth->execute($tag);
	while(@row = $sth->fetchrow_array()) {
		my $entryid = $row[2];
		my @values = decode_contact_entryid($row[2]);
		my $extid = decode_base64($values[5]);
		if (defined($extmap{$extid})) {
			$values[5] = encode_base64($extmap{$extid});
			# perl base64 adds \n at the end of the base64 data
			chomp($values[5]);
		}
		$entryid = encode_contact_entryid(@values);
		
		# check # converted?
		$udh->execute($entryid, $row[1], $tag)
			or die $DBI::errstr;
		
		if (defined($row[0])) {
			$udh_t->execute($entryid, $row[0], $row[1], $tag)
				or die $DBI::errstr;
		}
	}

	$dbh->commit();
	$i++;
}

print "\n** Please update the ldap_user_unique_attribute config value to objectGuid\n";
exit(0);
