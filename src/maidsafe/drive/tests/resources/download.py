#!/usr/bin/env python

import sys
import getopt
import os
import urllib2

def download(url_string, location):
  file_name = url_string.split('/')[-1]
  url = urllib2.urlopen(url_string)
  file = open(location + os.sep + file_name, 'wb')
  meta = url.info()
  file_size = int(meta.getheaders("Content-Length")[0])
  print "downloading %s: size = %s bytes" % (url_string, file_size)
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

def main(argv):
  url = ""
  location = ""
  try:
    opts, args = getopt.getopt(argv, "hu:l:", ["url","location="])
  except getopt.GetoptError:
      print 'zip.py -u <url> -l <location>'
      sys.exit(1)
  for opt, arg in opts:
    if opt == '-h':
      print 'zip.py -u <url> -l <location>'
      sys.exit()
    elif opt in ("-u", "--url"):
      url = arg
    elif opt in ("-l", "--location"):
      location = arg
    else:
      sys.exit(1)
  download(url, location)

if __name__ == "__main__":
  sys.exit(main(sys.argv[1:]))
