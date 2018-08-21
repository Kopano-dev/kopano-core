#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-only

CADIR=./demoCA
DAYS=$((3*365))
NAME=$1

if [ -d /usr/share/ssl/misc ]; then
	# SuSE / RHEL4
	if [ -x /usr/share/ssl/misc/CA.pl ]; then
		CASCRIPT=/usr/share/ssl/misc/CA.pl
	elif [ -x /usr/share/ssl/misc/CA.sh ]; then
		CASCRIPT=/usr/share/ssl/misc/CA.sh
	elif [ -x /usr/share/ssl/misc/CA ]; then
		CASCRIPT=/usr/share/ssl/misc/CA
	fi
	if [ -f /usr/share/ssl/misc/openssl.cnf ]; then
		# RHEL4
		CADIR=`grep -w ^dir -m 1 /usr/share/ssl/misc/openssl.cnf | awk {'print $3'}`
	elif [ -f /etc/ssl/openssl.cnf ]; then
		# SuSE
		CADIR=`grep -w ^dir -m 1 /etc/ssl/openssl.cnf | awk {'print $3'}`
	fi
elif [ -d /usr/lib/ssl/misc ]; then
	# Debian / Ubuntu
	if [ -x /usr/lib/ssl/misc/CA.pl ]; then
		CASCRIPT=/usr/lib/ssl/misc/CA.pl
	elif [ -x /usr/lib/ssl/misc/CA.sh ]; then
		CASCRIPT=/usr/lib/ssl/misc/CA.sh
	elif [ -x /usr/lib/ssl/misc/CA ]; then
		CASCRIPT=/usr/lib/ssl/misc/CA
	fi
	if [ -f /usr/lib/ssl/misc/openssl.cnf ]; then
		# --
		CADIR=`grep -w ^dir -m 1 /usr/lib/ssl/misc/openssl.cnf | awk {'print $3'}`
	elif [ -f /etc/ssl/openssl.cnf ]; then
		# Debian / Ubuntu
		CADIR=`grep -w ^dir -m 1 /etc/ssl/openssl.cnf | awk {'print $3'}`
	fi
elif [ -d /etc/pki/tls/misc ]; then
	# Fedora Core, RHEL5, RHEL6
	if [ -x /etc/pki/tls/misc/CA.pl ]; then
		CASCRIPT=/etc/pki/tls/misc/CA.pl
	elif [ -x /etc/pki/tls/misc/CA.sh ]; then
		CASCRIPT=/etc/pki/tls/misc/CA.sh
	elif [ -x /etc/pki/tls/misc/CA ]; then
		CASCRIPT=/etc/pki/tls/misc/CA
	fi
	if [ -f /etc/pki/tls/openssl.cnf ]; then
		CADIR=`grep -w ^dir -m 1 /etc/pki/tls/openssl.cnf | awk {'print $3'}`
	elif [ -f /etc/ssl/openssl.cnf ]; then
		# --
		CADIR=`grep -w ^dir -m 1 /etc/ssl/openssl.cnf | awk {'print $3'}`
	fi
elif [ -d /var/lib/ssl/misc ]; then
	# ALTLinux
	if [ -x /var/lib/ssl/misc/CA ]; then
		CASCRIPT=/var/lib/ssl/misc/CA
	fi
	if [ -f /etc/openssl/openssl.cnf ]; then
		# ALTLinux
		CADIR=`grep -w ^dir -m 1 /etc/openssl/openssl.cnf | awk {'print $3'}`
	fi
fi

if [ -z "$CASCRIPT" ]; then
	echo "OpenSSL CA script not found. Type script location below, or press enter to exit."
	read CASCRIPT
	if [ -z "$CASCRIPT" ]; then
		exit 0
	fi
	if [ ! -x "$CASCRIPT" ]; then
		echo "Script '$CASCRIPT' does not exist, or is not executable."
		exit 1
	fi
fi

if [ -z "$NAME" ]; then
	while [ -z "$NAME" -o -f "$NAME.pem" ]; do
		echo -n "Enter the name of the service:  "
		read NAME
		if [ -z "$NAME" ]; then
			echo "No name given."
		elif [ -f "$NAME.pem" ]; then
			echo "$NAME.pem already exists."
		fi
	done
fi

set -e

if [ ! -d "$CADIR" -o ! -f "$CADIR/serial" ]; then
	echo "No Certificate Authority Root found in current directory."
	echo "Press enter to create, or ctrl-c to exit."
	read dummy
	if [ -d "$CADIR" ]; then
		mv $CADIR ${CADIR}-backup.kopano
	fi
	$CASCRIPT -newca
fi

echo
echo "Now creating service certificate"
echo

# create new local service certificate
openssl req -new -keyout newkey.pem -out newreq.pem -days $DAYS

echo
echo "Signing certificate"
echo
openssl ca -days $DAYS -policy policy_anything -out newcert.pem -infiles newreq.pem

cat newkey.pem newcert.pem > $NAME.pem
chmod 600 $NAME.pem
rm newkey.pem newcert.pem newreq.pem

echo
echo -n "Create public key from this certificate? [y]  "
read public
PUBCMD="openssl rsa -in $NAME.pem -out $NAME-public.pem -outform PEM -pubout"
if [ -z "$public" -o "$public" = "y" ]; then
	set -- -e
	$PUBCMD
	if [ $? -ne 0 ]; then
		echo
		echo "No public key created. Use the following command to create it:"
		echo $PUBCMD
	fi
else
	echo "No public key created. Use the following command to create it:"
	echo $PUBCMD
fi
