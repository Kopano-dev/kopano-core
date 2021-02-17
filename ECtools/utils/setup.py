# SPDX-License-Identifier: AGPL-3.0-or-later
import io
import os.path
import re
import subprocess

from distutils.command.build_py import build_py
from setuptools import setup, find_packages


version_base = 'kopano_utils'
here = os.path.abspath(os.path.dirname(__file__))

try:
    FileNotFoundError
except NameError:
    FileNotFoundError = IOError

try:
    with io.open(os.path.join(here, version_base, 'version.py'), encoding='utf-8') as version_file:
        metadata = dict(re.findall(r"""__([a-z]+)__ = "([^"]+)""", version_file.read()))
except FileNotFoundError:
    try:
        v = subprocess.check_output(['../../tools/describe_version']).decode('utf-8').strip()[11:]
    except FileNotFoundError:
        v = 'dev'

    metadata = {
        'version': v,
        'withoutVersionFile': True
    }


class my_build_py(build_py, object):
    def run(self):
        super(my_build_py, self).run()

        if metadata.get('withoutVersionFile', False):
            with io.open(os.path.join(self.build_lib, version_base, 'version.py'), mode='w') as version_file:
                version_file.write('__version__ = "%s"\n' % metadata['version'])


setup(name='kopano-utils',
      version=metadata['version'],
      url='https://kopano.io',
      description='Utilities usd with Kopano Core',
      author='Kopano',
      author_email='development@kopano.io',
      keywords=['kopano'],
      license='AGPL',
      packages=find_packages(),
      install_requires=[
      ],
      cmdclass={'build_py': my_build_py},
      entry_points={
          'console_scripts': [
              'kopano-mr-process=kopano_utils.mr_process:main',
              'kopano-mr-accept=kopano_utils.mr_accept:main',
              'kopano-autorespond=kopano_utils.autorespond:main',
          ]
      }
)
