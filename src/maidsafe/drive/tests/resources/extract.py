#!/usr/bin/env python

import sys
import getopt
import os
import os.path
import urllib2
import zipfile
import tarfile
import bz2
import time

from bz2 import decompress

def timed_function(function):
  def wrapper(*arg):
    t1 = time.time()
    result = function(*arg)
    t2 = time.time()
    print "%s took %0.3f ms" % (function.func_name, (t2-t1)*1000.0)
    return result
  return wrapper

@timed_function
def extract_zip(file, location):
  print "extracting %s to %s" % (file, location)
  with zipfile.ZipFile(file, 'r') as file:
    file.extractall(location)

@timed_function
def extract_gz(file, location):
  print "extracting %s to %s" % (file, location)
  file = tarfile.open(file, 'r:gz')
  file.extractall(location)
  file.close()
 
@timed_function
def extract_bz2(file, location):
  print "extracting %s to %s" % (file, location)
  file = tarfile.open(file, 'r:bz2')
  file.extractall(location)
  file.close()

def main(argv):
  file = ""
  location = ""
  try:
    opts, args = getopt.getopt(argv, "hf:l:", ["file","location="])
  except getopt.GetoptError:
      print 'extract.py -f <file> -l <location>'
      sys.exit(1)
  for opt, arg in opts:
    if opt == '-h':
      print 'extract.py -f <file> -l <location>'
      sys.exit()
    elif opt in ("-f", "--file"):
      file = arg
    elif opt in ("-l", "--location"):
      location = arg
    else:
      sys.exit(1)

  filename = file.split('/')[-1]
  extension = os.path.splitext(filename)[1]
  if extension == '.zip':
    extract_zip(file, location)
  elif extension == '.gz':
    extract_gz(file, location)
  elif extension == '.bz2':
    extract_bz2(file, location)
  else:
    print 'unknown extension' 
    sys.exit(1)

if __name__ == "__main__":
  sys.exit(main(sys.argv[1:]))
