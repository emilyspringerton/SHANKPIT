# local NOCK (tools/local_nock.py): browser level editor + live edit-session server that the native
# client joins through SHANKPIT_EDIT_SESSIONS_URL. Levels save into levels/*.json and are committed.
LOCAL_NOCK_PORT ?= 8787

local-nock:
	python3 tools/local_nock.py --port $(LOCAL_NOCK_PORT)

# test-local-nock -- end-to-end over the real HTTP surface: level save + git commit, session
# create/post/poll, working-draft push/fetch, and the bad-request paths.
test-local-nock:
	python3 tools/test_local_nock.py
