@setlocal
@rem ProofRunKit: the prk command (#93). Runs proofrunkit.exe from this folder with every argument;
@rem stdin, stdout, stderr, and the exit code pass straight through.
@set "PRK_SHIM_EXE=%~dp0proofrunkit.exe"
@if not exist "%PRK_SHIM_EXE%" for %%I in (proofrunkit.exe) do @set "PRK_SHIM_EXE=%%~$PATH:I"
@"%PRK_SHIM_EXE%" %*
@endlocal & exit /b %ERRORLEVEL%
