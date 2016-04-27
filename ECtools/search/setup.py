from distutils.core import setup, Extension

setup(name='kopano-search',
      version='0.1',
      packages=['kopano_search'],
      package_data={'': ['xmltotext.xslt']},
)
