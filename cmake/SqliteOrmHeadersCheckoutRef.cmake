# Turn the revision a tree asked for into a ref that FetchContent can check out from whatever
# clone it already has.
#
# FetchContent hands GIT_TAG to `git checkout` in its update step, and what that step makes of a
# bare branch name depends on the refs the clone happens to hold. A clone made for a branch keeps a
# local refs/heads/<branch>, and the step rewrites the name to origin/<branch> itself. A clone made
# for a commit hash -- which is what the pin is -- holds no local branches at all, so the name
# matches only under refs/remotes/, the step leaves it bare, and `git checkout dev` then runs in a
# work tree where sqlite_orm keeps a top-level dev/ directory: `fatal: 'dev' could be both a local
# file and a tracking branch`, with nothing in it about this project or its revision option. Every
# branch whose name is also a top-level entry of sqlite_orm -- dev, include, tests, examples, docs,
# cmake -- lands there, and following the branch from a pinned tree is the one thing README tells
# a reader to do with this option, so the name is settled here rather than left to the clone.
#
# Only the remote knows which of branch, tag or hash a name is, so it is asked, by full ref name so
# that a tag and a branch spelled alike cannot be taken for each other. Tags and hashes are left
# exactly as written: origin/ in front of a tag names nothing. A full hash is not asked about at
# all -- no branch is spelled that way -- which keeps the configure that follows the pin from
# reaching the network on its own account, and an unreachable remote leaves the name alone, so a
# tree whose dependency is already populated is no worse off than before.
function(sqlite2orm_headers_checkout_ref revision repository git_executable out_variable)
    set(${out_variable} "${revision}" PARENT_SCOPE)
    if(NOT git_executable OR revision STREQUAL "")
        return()
    endif()
    # cmake's regexes have no counted repetition, so the length of a full hash is measured rather
    # than spelled in the pattern; `[0-9a-f]{40}` there matches a literal brace.
    string(LENGTH "${revision}" revision_length)
    if(revision_length EQUAL 40 AND revision MATCHES "^[0-9a-fA-F]+$")
        return()
    endif()

    # GIT_DIR and GIT_WORK_TREE out of the environment: where they are set -- a hook, `git bisect
    # run` -- they aim a bare `git` command at a repository that has nothing to do with the remote
    # being asked about.
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env --unset=GIT_DIR --unset=GIT_WORK_TREE "${git_executable}"
                ls-remote --heads "${repository}" "refs/heads/${revision}"
        OUTPUT_VARIABLE heads
        RESULT_VARIABLE status
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    if(NOT status EQUAL 0 OR heads STREQUAL "")
        return()
    endif()

    # FetchContent clones with git's default remote name and offers no way to change it.
    set(${out_variable} "origin/${revision}" PARENT_SCOPE)
endfunction()
