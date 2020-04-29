echo off
set "file=%CD%"
for %%i in ("%file%") do (
    set "filedrive=%%~di"
    set "filepath=%%~pi"
    set "filename=%%~ni"
    set "fileextension=%%~xi"
)
set "dir=%filedrive%%filepath%%filename%\.git"
echo on
if exist %dir% ( rd /S /Q %dir% )
call git init
call git add .
call git commit -m "initial commit"
call git remote add origin %GITHUB_ROOT%%filename%.git
call git push -u --force origin master
