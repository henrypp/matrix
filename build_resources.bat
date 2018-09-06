@echo off

php "..\builder\make_resource.php" ".\src\resource.hpp"
php "..\builder\make_locale.php" "Matrix" "matrix" ".\bin\i18n" ".\src\resource.hpp" ".\src\resource.rc" ".\bin\matrix.lng"
copy /y ".\bin\matrix.lng" ".\bin\32\matrix.lng"
copy /y ".\bin\matrix.lng" ".\bin\64\matrix.lng"

pause
