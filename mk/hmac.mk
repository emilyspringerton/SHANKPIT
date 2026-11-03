# HMAC-SHA256, checked against RFC 4231 (the ticket's signature). make test-hmac
.PHONY: test-hmac
test-hmac:
	gcc -std=gnu99 -Wall -Wextra -g -fsanitize=address,undefined -Ipackages/common \
		packages/common/hmac_sha256_test.c -o /tmp/shankpit_hmac_test
	/tmp/shankpit_hmac_test
