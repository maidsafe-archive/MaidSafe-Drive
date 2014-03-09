#!/usr/bin/env python

import sys
import getopt
import os
import urllib2
import zipfile
import time

def timed_function(function):
  def wrapper(*arg):
    t1 = time.time()
    result = function(*arg)
    t2 = time.time()
    print "%s took %0.3f ms" % (function.func_name, (t2-t1)*1000.0)
    return result
  return wrapper

def download(url_string, location):
  file_name = url_string.split('/')[-1]
  url = urllib2.urlopen(url_string)
  file = open(os.path.join(location, file_name), 'wb')
  meta = url.info()
  file_size = int(meta.getheaders("Content-Length")[0])
  print "downloading %s: size = %s bytes" % (file_name, file_size)
  downloaded = 0
  block_sz = 65536
  while True:
      buffer = url.read(block_sz)
      if not buffer:
          break
      downloaded += len(buffer)
      file.write(buffer)
      status = r"%10d  [%3.2f%%]" % (downloaded, downloaded * 100. / file_size)
      status = status + chr(8)*(len(status)+1)
      print status,
  file.close()
  url.close()

@timed_function
def extract(url, location):
  file_name = url.split('/')[-1]
  print "extracting %s" % (file_name)
  with zipfile.ZipFile(os.path.join(location, file_name), 'r') as file:
    file.extractall(os.path.join(location, os.path.splitext(file_name)[0]))

def main(argv):
  url = "http://pocoproject.org/releases/poco-1.4.6/poco-1.4.6p2.zip"
  location = ""
  try:
    opts, args = getopt.getopt(argv, "hl:", "location=")
  except getopt.GetoptError:
      print 'poco.py -l <location>'
      sys.exit(1)
  for opt, arg in opts:
    if opt == '-h':
      print 'poco.py -l <location>'
      sys.exit()
    elif opt in ("-l", "--location"):
      location = arg
    else:
      sys.exit(1)
  download(url, location)
  extract(url, location)  

if __name__ == "__main__":
  sys.exit(main(sys.argv[1:]))
