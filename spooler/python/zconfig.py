# SPDX-License-Identifier: AGPL-3.0-only
import configparser
import io
import os

class ZConfigParser:

    def __init__(self, configfile, defaultoptions={}):

        self.config = configparser.ConfigParser(defaults=defaultoptions)

        self.readZConfig(configfile)

    def readZConfig(self, filename):
        filename = os.path.abspath(filename)

        data = "[DEFAULT]\r\n"
        try:
            fp = open(filename)
        except IOError as e:
            if e.errno == 2 or e.errno == 13:
                raise IOError(e.errno, 'Unable to open config file \''+filename+'\'. ' + e.strerror)
            else:
                raise

        for line in fp:
            data += line

        fp.close()

        self.config.readfp(io.StringIO(data))

    def options(self):
        return self.config.defaults()

    def get(self, option):
        return self.config.get('DEFAULT', option)

    def getint(self, option):
        return self.config.getint('DEFAULT', option)

    def getboolean(self, option):
        return self.config.getboolean('DEFAULT', option)

    ## Get a dict of a list of options
    #  if you have the following options in the config file:
    #    test_option1, test_option2, test_option3
    #  getdict('test', ['option1', 'option2', 'option3'])
    #
    def getdict(self, prefix, options):
        data = {}
        for option in options:
            data[option] = self.get(prefix+'_'+option)

        return data
