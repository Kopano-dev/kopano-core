import io
import os.path
import re
import subprocess

from setuptools import setup, find_packages


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


setup(name='kopano-migration-pst',
      version=metadata['version'],
      url='https://kopano.io',
      author='Kopano',
      author_email='development@kopano.io',
      keywords=['kopano'],
      license='AGPL',
      packages=find_packages()
)
