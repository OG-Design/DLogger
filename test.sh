./build.sh
DLOGGER_MODE=dev python3 -m unittest -v tests/test_logger.py
DLOGGER_MODE=dev python3 -m unittest -v tests/test_reporter.py tests/test_smtp.py