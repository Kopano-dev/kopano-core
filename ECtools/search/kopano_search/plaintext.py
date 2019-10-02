#!/usr/bin/python3
# SPDX-License-Identifier: AGPL-3.0-only
import mimetypes
import os.path
import resource
import shutil
import subprocess
import tempfile

# distros ship incompatible magic.py projects!
import magic
try:
    magic.from_buffer
    def from_buffer(data):
        return magic.from_buffer(data, mime=True)
except AttributeError:
    MAGIC = magic.open(magic.MAGIC_MIME_TYPE)
    MAGIC.load()
    def from_buffer(data):
        return MAGIC.buffer(data)

"""

convert attachment to plaint text using external commands, as given below

basically the type of an attachment is determined first by its filename extension,
then stored mimetype and if neither are present by using python-magic.


"""

MAX_TIME = 10 # time limit: 10 seconds # XXX get from cfg
MAX_MEMORY = 256*10**6 # max mem usage: 256 MB # XXX get from cfg

CONVERT_ODF = 'unzip -p %(file)s content.xml | %(xmltotext)s -'
CONVERT_OOXML = 'cd %(dir)s; unzip -o -qq %(file)s; for i in $(find . -name \*.xml); do %(xmltotext)s $i; done'

DB = [ # XXX read from file, test encodings
    ('txt;text/plain', 'iconv -c -t utf-8 %(file)s'),
    ('html;htm;text/html', 'w3m -dump -s -O utf-8 %(file)s'),
    ('xml;application/xml;text/xml', '%(xmltotext)s %(file)s'),
    ('pdf;application/pdf', 'pdftotext -q -nopgbrk %(file)s /dev/stdout'),
    ('odt;application/vnd.oasis.opendocument.text', CONVERT_ODF),
    ('ods;application/vnd.oasis.opendocument.spreadsheet', CONVERT_ODF),
    ('odp;application/vnd.oasis.opendocument.presentation', CONVERT_ODF),
    ('doc;application/msword', 'catdoc -s cp1252 -d utf-8 -f ascii -w %(file)s'), # XXX specifying codepage here doesn't look right..
    ('ppt;application/mspowerpoint;application/powerpoint;application/x-mspowerpoint;application/vnd.ms-powerpoint', 'catppt -s cp1252 -d utf-8 %(file)s'),
    ('xls;application/excel;application/x-excel;application/x-msexcel;application/vnd.ms-excel', "xls2csv -s cp1252 -d utf-8 -c ' ' %(file)s"),
    ('docx;application/vnd.openxmlformats-officedocument.wordprocessingml.document', CONVERT_OOXML),
    ('xlsx;application/vnd.openxmlformats-officedocument.spreadsheetml.sheet', CONVERT_OOXML),
    ('pptx;application/vnd.openxmlformats-officedocument.presentationml.presentation', CONVERT_OOXML),
]
CMD = {}
for (X, C) in DB:
    for Y in X.split(';'):
        CMD[Y] = C

# python-coverage doesn't work for call-backs from thread created in C,
# so skip coverage
def setlimits(): # pragma: no cover
    resource.setrlimit(resource.RLIMIT_DATA, (MAX_MEMORY, MAX_MEMORY))
    resource.setrlimit(resource.RLIMIT_CPU, (MAX_TIME, MAX_TIME))

def convert(cmd, data, log):
    """ save data to tempfile and call external command on it; abort if it uses too much memory/time """

    result = []
    tmpdir = tempfile.mkdtemp()
    try:
        tmpfile = '%s/attachment' % tmpdir
        f = open(tmpfile, 'wb')
        f.write(data)
        f.close()
        cmd = cmd % {
            'dir': tmpdir,
            'file': tmpfile,
            'xmltotext': 'xsltproc '+os.path.join(os.path.dirname(os.path.realpath(__file__)), 'xmltotext.xslt')
        }
        log.debug("executing command: '%s'" % cmd)
        p = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, preexec_fn=setlimits, env={"HOME": tmpdir})
        out, err = p.communicate()
        if err:
            err = err.decode('utf-8', 'ignore')
            log.warning('output on stderr:\n'+err[:1024]+'..')
        if p.returncode != 0:
            log.warning('return code = %d' % p.returncode)
        plain = out.decode('utf-8', 'ignore') # XXX warning instead of ignore
        log.debug('converted %d bytes to %d chars of plaintext' % (len(data), len(plain)))
        return plain
    finally:
        shutil.rmtree(tmpdir)

def ext_mime_data(f, filename, mimetype, log):
    """ first use filename extension to determine type, if that fails the stored mimetype, and finally libmagic """

    ext = os.path.splitext(filename)[1]
    data = None
    if ext:
        method = 'extension'
        mimetype = mimetypes.guess_type(filename)[0]
    elif mimetype and mimetype != 'application/octet-stream':
        method = 'mimetype'
        ext = mimetypes.guess_extension(mimetype)
    else:
        method = 'magic'
        data = f.read()
        mimetype = from_buffer(data)
        ext = mimetypes.guess_extension(mimetype)
    if ext:
        ext = ext[1:]
    log.debug('detected extension, mimetype: %s, %s (method=%s)' % (ext, mimetype, method))
    return ext, mimetype, data

def get(f, mimetype=None, log=None):
    """ convert file-like object to plaintext, only reading data if needed; check DB with determined extension and mimetype """

    filename = f.name or u''
    ext, mimetype, data = ext_mime_data(f, filename, mimetype, log)
    for key in ext, mimetype:
        if key in CMD:
            return convert(CMD[key], data or f.read(), log)
    log.debug('unknown or unsupported filetype, skipping')
    return u''
