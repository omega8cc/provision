core = 6.x
api = 2

; BOA-2.2.5-dev

projects[pressflow][type] = "core"
projects[pressflow][download][type] = "get"
projects[pressflow][download][url] = "http://files.aegir.cc/core/pressflow-6.31.2.tar.gz"

projects[hostmaster][type] = "profile"
projects[hostmaster][download][type] = "git"
projects[hostmaster][download][url] = "MAKEFILE_REPO_GIT_URL/hostmaster.git"
projects[hostmaster][download][branch] = "master"
