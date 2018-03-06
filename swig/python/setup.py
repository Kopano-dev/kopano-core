import io
import os.path
import re
import subprocess

from setuptools import setup


here = os.path.abspath(os.path.dirname(__file__))

try:
    FileNotFoundError
except NameError:
    FileNotFoundError = IOError

try:
    with io.open(os.path.join(here, '..', '..', '.version'), encoding='utf-8') as version_file:
        metadata = {
            'version': version_file.readline().strip()
        }
except FileNotFoundError:
    try:
        v = subprocess.check_output(['../../tools/describe_version']).decode('utf-8').strip()[11:]
    except (FileNotFoundError, subprocess.CalledProcessError):
        v = '0.0.dev0'

    metadata = {
        'version': v,
        'withoutVersionFile': True
    }


setup(name='MAPI',
      version=metadata['version'],
      packages=['MAPI', 'MAPI.Util'],
      url='https://kopano.io',
      description='Python bindings for MAPI',
      long_description='Low-level (SWIG-generated) Python bindings for MAPI. Using this module, you can create Python programs which use MAPI calls to interact with Kopano.',
      author='Kopano',
      author_email='development@kopano.io',
      keywords=['kopano'],
      license='AGPL'
)
                  
