import io
import os.path
import re
import subprocess

from distutils.command.build_py import build_py
from setuptools import setup, find_packages


here = os.path.abspath(os.path.dirname(__file__))

try:
    FileNotFoundError
except NameError:
    FileNotFoundError = IOError

try:
    with io.open(os.path.join(here, 'kopano', 'version.py'), encoding='utf-8') as version_file:
        metadata = dict(re.findall(r"""__([a-z]+)__ = "([^"]+)""", version_file.read()))
except FileNotFoundError:
    try:
        v = subprocess.check_output(['../../../tools/describe_version']).decode('utf-8').strip()[11:]
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
            with io.open(os.path.join(self.build_lib, 'kopano', 'version.py'), mode='w') as version_file:
                version_file.write('__version__ = "%s"\n' % metadata['version'])
            

setup(name='kopano',
      version=metadata['version'],
      url='https://kopano.io',
      description='High-level Python bindings for Kopano',
      long_description='Object-Oriented Python bindings for Kopano. Uses python-mapi to do the low level work. Can be used for many common system administration tasks.',
      author='Kopano',
      author_email='development@kopano.io',
      keywords=['kopano'],
      license='AGPL',
      packages=find_packages(),
      install_requires=[
      ],
      cmdclass={'build_py': my_build_py}
)
                  
