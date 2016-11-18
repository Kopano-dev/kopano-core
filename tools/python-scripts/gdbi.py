try:
    from IPython.zmq.ipkernel import IPKernelApp
except ImportError:
    from IPython.kernel.zmq.kernelapp import IPKernelApp

"""
launching IPython from GDB!

http://blog.scottt.tw/2012/01/exploring-gdb-python-api-with-ipython_31.html

usage:

0) start gdb, eg:

   "gdb /usr/sbin/kopano-server core"

1) from gdb: run

   "source gdbi.py"

   -> this will provide a 'kernel identifier', eg "kernel-1234.json"

2) start ipython using eg:

   "ipython console --existing kernel-1234.json"

3) start specific script, eg:

   "import kopano_cache"

"""

app = IPKernelApp.instance()
app.initialize([])
app.start()
