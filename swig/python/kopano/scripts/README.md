# Kopano Scripts

Collection of Python-kopano scripts.

## Kopano-rest

PoC REST API using Python-falcon, the API can be run with gunicorn.

  gunicorn --workers=4 --threads=2 kopano-rest:app -b 127.0.0.1:9998

Interacting with the API can be done with for example curl and for authentication requires
a userid in the X-Kopano-UserEntryID. The userid can be obtained with the following script:

  python userid.py -u user1

An example curl request:

  curl -v -XGET --header "X-Kopano-UserEntryID: AAAAADihuxAF5RAaobsIACsqVsIAAHphcmFmYTZjbGllbnQuZGxsAAAAAADQnF7cQlBKvKf5l575bT71AQAAAAEAAAABe8PDNi5Eh58hibx2N1bFcHNldWRvOi8vc2VydmVyMQA=" http://127.0.0.1:9998/api/gc/v0/me/mailFolders/

