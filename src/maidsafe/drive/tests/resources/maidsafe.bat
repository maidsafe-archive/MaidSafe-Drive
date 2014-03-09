rem First and second parameters set vs environment 
call %1 %2
set errorlevel=0
git clone git@github.com:maidsafe/MaidSafe.git
cd MaidSafe
git submodule update --init
git checkout next
git pull
git submodule foreach 'git checkout next && git pull'
mkdir build
cd build
cmake .. -G %3
cmake --build . --config Release
cmake --build . --config Debug
exit %errorlevel%