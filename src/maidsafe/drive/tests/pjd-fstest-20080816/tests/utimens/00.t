#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/chmod/00.t,v 1.2 2007/01/25 20:48:14 pjd Exp $

# Copyright 2011 maidsafe.net limited
#
# utimens system call test via touch bash command
# utimens 00
echo 
echo "**********  Copyright 2011 maidsafe.net limited    **********"

desc="utimens access and modification times"

dir=`dirname $0`
. ${dir}/../misc.sh

echo "1..8"

test_group1="user_group1"
test_group2="user_group2"
test_user1="test_user1"
test_user2="test_user2"
groupadd -g 35534 ${test_group1}
groupadd -g 35533 ${test_group2}
useradd ${test_user1} -u 35534 -g 35534
useradd ${test_user2} -u 35533 -g 35533

n0=`namegen`
n1=`namegen`
n2=`namegen`

expect 0 mkdir ${n0} 0777
cdir=`pwd`
cd ${n0}

expect 0 -u 35534 -g 35534 create ${n1} 0644
ctime1=`${fstest} stat ${n1} ctime`
atime1=`${fstest} stat ${n1} atime`
mtime1=`${fstest} stat ${n1} mtime`

# rejected request shall not update time stamp
sleep 1
sudo -u ${test_user2} touch ${n1}
ctime2=`${fstest} stat ${n1} ctime`
atime2=`${fstest} stat ${n1} atime`
mtime2=`${fstest} stat ${n1} mtime`
test_check $ctime1 -eq $ctime2
test_check $atime1 -eq $atime2
test_check $mtime1 -eq $mtime2

# succeed request shall update time stamp
sleep 1
sudo -u ${test_user1} touch ${n1}
ctime2=`${fstest} stat ${n1} ctime`
atime2=`${fstest} stat ${n1} atime`
mtime2=`${fstest} stat ${n1} mtime`
test_check $ctime1 -lt $ctime2
test_check $atime1 -lt $atime2
test_check $mtime1 -lt $mtime2

userdel ${test_user1}
userdel ${test_user2}
groupdel ${test_group1}
groupdel ${test_group2}
