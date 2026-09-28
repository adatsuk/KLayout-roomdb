
TEMPLATE = subdirs

# Automatically include all sub-folders, but not the .pro file
SUBDIR_LIST = $$files($$PWD/*)
SUBDIR_LIST -= $$PWD/streamers.pro
SUBDIR_LIST -= $$PWD/mroom

SUBDIRS = $$SUBDIR_LIST

# CommonDB ROOM reader (streamers/mroom → integrations/klayout/mroom)
SUBDIRS += mroom
