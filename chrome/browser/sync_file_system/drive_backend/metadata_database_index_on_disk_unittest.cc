// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync_file_system/drive_backend/metadata_database_index_on_disk.h"

#include <algorithm>

#include "base/files/scoped_temp_dir.h"
#include "base/strings/string_number_conversions.h"
#include "chrome/browser/sync_file_system/drive_backend/drive_backend_constants.h"
#include "chrome/browser/sync_file_system/drive_backend/drive_backend_test_util.h"
#include "chrome/browser/sync_file_system/drive_backend/drive_backend_util.h"
#include "chrome/browser/sync_file_system/drive_backend/leveldb_wrapper.h"
#include "chrome/browser/sync_file_system/drive_backend/metadata_database.pb.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/leveldatabase/src/helpers/memenv/memenv.h"
#include "third_party/leveldatabase/src/include/leveldb/db.h"
#include "third_party/leveldatabase/src/include/leveldb/env.h"
#include "third_party/leveldatabase/src/include/leveldb/status.h"

namespace sync_file_system {
namespace drive_backend {

namespace {

const int64 kSyncRootTrackerID = 1;
const int64 kAppRootTrackerID = 2;
const int64 kFileTrackerID = 3;
const int64 kPlaceholderTrackerID = 4;

}  // namespace

class MetadataDatabaseIndexOnDiskTest : public testing::Test {
 public:
  ~MetadataDatabaseIndexOnDiskTest() override {}

  void SetUp() override {
    ASSERT_TRUE(database_dir_.CreateUniqueTempDir());
    in_memory_env_.reset(leveldb::NewMemEnv(leveldb::Env::Default()));
    db_ = InitializeLevelDB();
    index_ = MetadataDatabaseIndexOnDisk::Create(db_.get());
  }

  void TearDown() override {
    index_.reset();
    db_.reset();
    in_memory_env_.reset();
  }

  void CreateTestDatabase(bool build_index, LevelDBWrapper* db) {
    if (!db) {
      DCHECK(index());
      db = index()->GetDBForTesting();
    }
    DCHECK(db);

    scoped_ptr<FileMetadata> sync_root_metadata =
        test_util::CreateFolderMetadata("sync_root_folder_id",
                                        "Chrome Syncable FileSystem");
    scoped_ptr<FileTracker> sync_root_tracker =
        test_util::CreateTracker(*sync_root_metadata, kSyncRootTrackerID,
                                 nullptr);

    scoped_ptr<FileMetadata> app_root_metadata =
        test_util::CreateFolderMetadata("app_root_folder_id", "app_title");
    scoped_ptr<FileTracker> app_root_tracker =
        test_util::CreateTracker(*app_root_metadata, kAppRootTrackerID,
                                 sync_root_tracker.get());
    app_root_tracker->set_app_id("app_id");
    app_root_tracker->set_tracker_kind(TRACKER_KIND_APP_ROOT);

    scoped_ptr<FileMetadata> file_metadata =
        test_util::CreateFileMetadata("file_id", "file", "file_md5");
    scoped_ptr<FileTracker> file_tracker =
        test_util::CreateTracker(*file_metadata,
                                 kFileTrackerID,
                                 app_root_tracker.get());

    scoped_ptr<FileTracker> placeholder_tracker =
        test_util::CreatePlaceholderTracker("unsynced_file_id",
                                            kPlaceholderTrackerID,
                                            app_root_tracker.get());

    scoped_ptr<ServiceMetadata> service_metadata =
        InitializeServiceMetadata(db);
    service_metadata->set_sync_root_tracker_id(kSyncRootTrackerID);
    PutServiceMetadataToDB(*service_metadata, db);

    if (build_index) {
      DCHECK(index());

      index()->StoreFileMetadata(sync_root_metadata.Pass());
      index()->StoreFileTracker(sync_root_tracker.Pass());
      index()->StoreFileMetadata(app_root_metadata.Pass());
      index()->StoreFileTracker(app_root_tracker.Pass());
      index()->StoreFileMetadata(file_metadata.Pass());
      index()->StoreFileTracker(file_tracker.Pass());
      index()->StoreFileTracker(placeholder_tracker.Pass());
    } else {
      PutFileMetadataToDB(*sync_root_metadata, db);
      PutFileTrackerToDB(*sync_root_tracker, db);
      PutFileMetadataToDB(*app_root_metadata, db);
      PutFileTrackerToDB(*app_root_tracker, db);
      PutFileMetadataToDB(*file_metadata, db);
      PutFileTrackerToDB(*file_tracker, db);
      PutFileTrackerToDB(*placeholder_tracker, db);
    }

    ASSERT_TRUE(db->Commit().ok());
  }

  MetadataDatabaseIndexOnDisk* index() { return index_.get(); }

  void WriteToDB() {
    ASSERT_TRUE(db_->Commit().ok());
  }

  scoped_ptr<LevelDBWrapper> InitializeLevelDB() {
    leveldb::DB* db = nullptr;
    leveldb::Options options;
    options.create_if_missing = true;
    options.max_open_files = 0;  // Use minimum.
    options.env = in_memory_env_.get();
    leveldb::Status status =
        leveldb::DB::Open(options, database_dir_.path().AsUTF8Unsafe(), &db);
    EXPECT_TRUE(status.ok());
    return make_scoped_ptr(new LevelDBWrapper(make_scoped_ptr(db)));
  }

 private:
  scoped_ptr<MetadataDatabaseIndexOnDisk> index_;

  base::ScopedTempDir database_dir_;
  scoped_ptr<leveldb::Env> in_memory_env_;
  scoped_ptr<LevelDBWrapper> db_;
};

TEST_F(MetadataDatabaseIndexOnDiskTest, GetEntryTest) {
  CreateTestDatabase(false, nullptr);

  FileTracker tracker;
  EXPECT_FALSE(index()->GetFileTracker(kInvalidTrackerID, nullptr));
  ASSERT_TRUE(index()->GetFileTracker(kFileTrackerID, &tracker));
  EXPECT_EQ(kFileTrackerID, tracker.tracker_id());
  EXPECT_EQ("file_id", tracker.file_id());

  FileMetadata metadata;
  EXPECT_FALSE(index()->GetFileMetadata(std::string(), nullptr));
  ASSERT_TRUE(index()->GetFileMetadata("file_id", &metadata));
  EXPECT_EQ("file_id", metadata.file_id());
}

TEST_F(MetadataDatabaseIndexOnDiskTest, SetEntryTest) {
  CreateTestDatabase(false, nullptr);

  const int64 tracker_id = 10;
  scoped_ptr<FileMetadata> metadata =
      test_util::CreateFileMetadata("test_file_id", "test_title", "test_md5");
  FileTracker root_tracker;
  EXPECT_TRUE(index()->GetFileTracker(kSyncRootTrackerID, &root_tracker));
  scoped_ptr<FileTracker> tracker =
      test_util::CreateTracker(*metadata, tracker_id, &root_tracker);

  index()->StoreFileMetadata(metadata.Pass());
  index()->StoreFileTracker(tracker.Pass());

  EXPECT_TRUE(index()->GetFileMetadata("test_file_id", nullptr));
  EXPECT_TRUE(index()->GetFileTracker(tracker_id, nullptr));

  WriteToDB();

  metadata.reset(new FileMetadata);
  ASSERT_TRUE(index()->GetFileMetadata("test_file_id", metadata.get()));
  EXPECT_TRUE(metadata->has_details());
  EXPECT_EQ("test_title", metadata->details().title());

  tracker.reset(new FileTracker);
  ASSERT_TRUE(index()->GetFileTracker(tracker_id, tracker.get()));
  EXPECT_EQ("test_file_id", tracker->file_id());

  // Test if removers work.
  index()->RemoveFileMetadata("test_file_id");
  index()->RemoveFileTracker(tracker_id);

  EXPECT_FALSE(index()->GetFileMetadata("test_file_id", nullptr));
  EXPECT_FALSE(index()->GetFileTracker(tracker_id, nullptr));

  WriteToDB();

  EXPECT_FALSE(index()->GetFileMetadata("test_file_id", nullptr));
  EXPECT_FALSE(index()->GetFileTracker(tracker_id, nullptr));
}

TEST_F(MetadataDatabaseIndexOnDiskTest, RemoveUnreachableItemsTest) {
  scoped_ptr<LevelDBWrapper> db = InitializeLevelDB();
  CreateTestDatabase(false, db.get());

  const int kOrphanedFileTrackerID = 13;
  scoped_ptr<FileMetadata> orphaned_metadata =
      test_util::CreateFileMetadata("orphaned_id", "orphaned", "md5");
  scoped_ptr<FileTracker> orphaned_tracker =
      test_util::CreateTracker(*orphaned_metadata,
                               kOrphanedFileTrackerID,
                               nullptr);

  PutFileMetadataToDB(*orphaned_metadata, db.get());
  PutFileTrackerToDB(*orphaned_tracker, db.get());
  EXPECT_TRUE(db->Commit().ok());

  const std::string key =
      kFileTrackerKeyPrefix + base::Int64ToString(kOrphanedFileTrackerID);
  std::string value;
  EXPECT_TRUE(db->Get(key, &value).ok());

  // RemoveUnreachableItems() is expected to run on index creation.
  scoped_ptr<MetadataDatabaseIndexOnDisk> index_on_disk =
      MetadataDatabaseIndexOnDisk::Create(db.get());
  EXPECT_TRUE(db->Commit().ok());

  EXPECT_TRUE(db->Get(key, &value).IsNotFound());
  EXPECT_FALSE(index_on_disk->GetFileTracker(kOrphanedFileTrackerID, nullptr));

  EXPECT_TRUE(index_on_disk->GetFileTracker(kSyncRootTrackerID, nullptr));
  EXPECT_TRUE(index_on_disk->GetFileTracker(kAppRootTrackerID, nullptr));
  EXPECT_TRUE(index_on_disk->GetFileTracker(kFileTrackerID, nullptr));
}


TEST_F(MetadataDatabaseIndexOnDiskTest, BuildIndexTest) {
  CreateTestDatabase(false, nullptr);

  TrackerIDSet tracker_ids;
  // Before building indexes, no references exist.
  EXPECT_EQ(kInvalidTrackerID, index()->GetAppRootTracker("app_id"));
  tracker_ids = index()->GetFileTrackerIDsByFileID("file_id");
  EXPECT_TRUE(tracker_ids.empty());
  tracker_ids = index()->GetFileTrackerIDsByParentAndTitle(
      kAppRootTrackerID, "file");
  EXPECT_TRUE(tracker_ids.empty());
  EXPECT_EQ(0U, index()->CountDirtyTracker());

  EXPECT_EQ(16, index()->BuildTrackerIndexes());
  WriteToDB();

  // After building indexes, we should have correct indexes.
  EXPECT_EQ(kAppRootTrackerID, index()->GetAppRootTracker("app_id"));
  tracker_ids = index()->GetFileTrackerIDsByFileID("file_id");
  EXPECT_EQ(1U, tracker_ids.size());
  EXPECT_EQ(kFileTrackerID, tracker_ids.active_tracker());
  tracker_ids = index()->GetFileTrackerIDsByParentAndTitle(
      kAppRootTrackerID, "file");
  EXPECT_EQ(1U, tracker_ids.size());
  EXPECT_EQ(kFileTrackerID, tracker_ids.active_tracker());
  EXPECT_EQ(1U, index()->CountDirtyTracker());
}

TEST_F(MetadataDatabaseIndexOnDiskTest, BuildAndDeleteIndexTest) {
  CreateTestDatabase(false, nullptr);
  int64 answer = index()->BuildTrackerIndexes();
  WriteToDB();
  ASSERT_EQ(16, answer);
  EXPECT_EQ(answer, index()->DeleteTrackerIndexes());
  WriteToDB();
  EXPECT_EQ(answer, index()->BuildTrackerIndexes());
  WriteToDB();
}

TEST_F(MetadataDatabaseIndexOnDiskTest, AllEntriesTest) {
  CreateTestDatabase(true, nullptr);

  EXPECT_EQ(3U, index()->CountFileMetadata());
  std::vector<std::string> file_ids(index()->GetAllMetadataIDs());
  ASSERT_EQ(3U, file_ids.size());
  std::sort(file_ids.begin(), file_ids.end());
  EXPECT_EQ("app_root_folder_id", file_ids[0]);
  EXPECT_EQ("file_id", file_ids[1]);
  EXPECT_EQ("sync_root_folder_id", file_ids[2]);

  EXPECT_EQ(4U, index()->CountFileTracker());
  std::vector<int64> tracker_ids = index()->GetAllTrackerIDs();
  ASSERT_EQ(4U, tracker_ids.size());
  std::sort(tracker_ids.begin(), tracker_ids.end());
  EXPECT_EQ(kSyncRootTrackerID, tracker_ids[0]);
  EXPECT_EQ(kAppRootTrackerID, tracker_ids[1]);
  EXPECT_EQ(kFileTrackerID, tracker_ids[2]);
  EXPECT_EQ(kPlaceholderTrackerID, tracker_ids[3]);
}

TEST_F(MetadataDatabaseIndexOnDiskTest, IndexAppRootIDByAppIDTest) {
  CreateTestDatabase(true, nullptr);

  std::vector<std::string> app_ids = index()->GetRegisteredAppIDs();
  ASSERT_EQ(1U, app_ids.size());
  EXPECT_EQ("app_id", app_ids[0]);

  EXPECT_EQ(kInvalidTrackerID, index()->GetAppRootTracker(""));
  EXPECT_EQ(kAppRootTrackerID, index()->GetAppRootTracker("app_id"));

  const int64 kAppRootTrackerID2 = 12;
  FileTracker sync_root_tracker;
  index()->GetFileTracker(kSyncRootTrackerID, &sync_root_tracker);
  scoped_ptr<FileMetadata> app_root_metadata =
      test_util::CreateFolderMetadata("app_root_folder_id_2", "app_title_2");

  // Testing AddToAppIDIndex
  scoped_ptr<FileTracker> app_root_tracker =
      test_util::CreateTracker(*app_root_metadata, kAppRootTrackerID2,
                               &sync_root_tracker);
  app_root_tracker->set_app_id("app_id_2");
  app_root_tracker->set_tracker_kind(TRACKER_KIND_APP_ROOT);

  index()->StoreFileTracker(app_root_tracker.Pass());
  WriteToDB();
  EXPECT_EQ(kAppRootTrackerID, index()->GetAppRootTracker("app_id"));
  EXPECT_EQ(kAppRootTrackerID2, index()->GetAppRootTracker("app_id_2"));

  // Testing UpdateInAppIDIndex
  app_root_tracker = test_util::CreateTracker(*app_root_metadata,
                                              kAppRootTrackerID2,
                                              &sync_root_tracker);
  app_root_tracker->set_app_id("app_id_3");
  app_root_tracker->set_active(false);

  index()->StoreFileTracker(app_root_tracker.Pass());
  WriteToDB();
  EXPECT_EQ(kAppRootTrackerID, index()->GetAppRootTracker("app_id"));
  EXPECT_EQ(kInvalidTrackerID, index()->GetAppRootTracker("app_id_2"));
  EXPECT_EQ(kInvalidTrackerID, index()->GetAppRootTracker("app_id_3"));

  app_root_tracker = test_util::CreateTracker(*app_root_metadata,
                                              kAppRootTrackerID2,
                                              &sync_root_tracker);
  app_root_tracker->set_app_id("app_id_3");
  app_root_tracker->set_tracker_kind(TRACKER_KIND_APP_ROOT);

  index()->StoreFileTracker(app_root_tracker.Pass());
  WriteToDB();
  EXPECT_EQ(kAppRootTrackerID, index()->GetAppRootTracker("app_id"));
  EXPECT_EQ(kInvalidTrackerID, index()->GetAppRootTracker("app_id_2"));
  EXPECT_EQ(kAppRootTrackerID2, index()->GetAppRootTracker("app_id_3"));

  // Testing RemoveFromAppIDIndex
  index()->RemoveFileTracker(kAppRootTrackerID2);
  WriteToDB();
  EXPECT_EQ(kAppRootTrackerID, index()->GetAppRootTracker("app_id"));
  EXPECT_EQ(kInvalidTrackerID, index()->GetAppRootTracker("app_id_3"));
}

TEST_F(MetadataDatabaseIndexOnDiskTest, TrackerIDSetByFileIDTest) {
  CreateTestDatabase(true, nullptr);

  FileTracker app_root_tracker;
  EXPECT_TRUE(index()->GetFileTracker(kAppRootTrackerID, &app_root_tracker));
  FileMetadata metadata;
  EXPECT_TRUE(index()->GetFileMetadata("file_id", &metadata));

  // Testing GetFileTrackerIDsByFileID
  TrackerIDSet tracker_ids = index()->GetFileTrackerIDsByFileID("file_id");
  EXPECT_EQ(1U, tracker_ids.size());
  EXPECT_EQ(kFileTrackerID, tracker_ids.active_tracker());

  const int64 tracker_id = 21;
  // Testing AddToFileIDIndexes
  scoped_ptr<FileTracker> file_tracker =
      test_util::CreateTracker(metadata, tracker_id, &app_root_tracker);

  index()->StoreFileTracker(file_tracker.Pass());
  WriteToDB();
  tracker_ids = index()->GetFileTrackerIDsByFileID("file_id");
  EXPECT_EQ(2U, tracker_ids.size());
  EXPECT_EQ(tracker_id, tracker_ids.active_tracker());

  std::string multi_file_id = index()->PickMultiTrackerFileID();
  EXPECT_EQ("file_id", multi_file_id);

  // Testing UpdateInFileIDIndexes
  file_tracker =
      test_util::CreateTracker(metadata, tracker_id, &app_root_tracker);
  file_tracker->set_active(false);

  index()->StoreFileTracker(file_tracker.Pass());
  WriteToDB();
  tracker_ids = index()->GetFileTrackerIDsByFileID("file_id");
  EXPECT_EQ(2U, tracker_ids.size());
  EXPECT_EQ(kInvalidTrackerID, tracker_ids.active_tracker());

  multi_file_id = index()->PickMultiTrackerFileID();
  EXPECT_EQ("file_id", multi_file_id);

  file_tracker =
      test_util::CreateTracker(metadata, tracker_id, &app_root_tracker);

  index()->StoreFileTracker(file_tracker.Pass());
  WriteToDB();
  tracker_ids = index()->GetFileTrackerIDsByFileID("file_id");
  EXPECT_EQ(2U, tracker_ids.size());
  EXPECT_EQ(tracker_id, tracker_ids.active_tracker());

  multi_file_id = index()->PickMultiTrackerFileID();
  EXPECT_EQ("file_id", multi_file_id);

  // Testing RemoveFromFileIDIndexes
  index()->RemoveFileTracker(tracker_id);
  WriteToDB();
  tracker_ids = index()->GetFileTrackerIDsByFileID("file_id");
  EXPECT_EQ(1U, tracker_ids.size());
  EXPECT_EQ(kInvalidTrackerID, tracker_ids.active_tracker());

  multi_file_id = index()->PickMultiTrackerFileID();
  EXPECT_TRUE(multi_file_id.empty()) << multi_file_id;
}

TEST_F(MetadataDatabaseIndexOnDiskTest, TrackerIDSetByParentIDAndTitleTest) {
  CreateTestDatabase(true, nullptr);

  FileTracker app_root_tracker;
  EXPECT_TRUE(index()->GetFileTracker(kAppRootTrackerID, &app_root_tracker));
  FileMetadata metadata;
  EXPECT_TRUE(index()->GetFileMetadata("file_id", &metadata));

  // Testing GetFileTrackerIDsByFileID
  TrackerIDSet tracker_ids = index()->GetFileTrackerIDsByParentAndTitle(
      kAppRootTrackerID, "file");
  EXPECT_EQ(1U, tracker_ids.size());
  EXPECT_EQ(kFileTrackerID, tracker_ids.active_tracker());

  tracker_ids = index()->GetFileTrackerIDsByParentAndTitle(
      kAppRootTrackerID, "file2");
  EXPECT_TRUE(tracker_ids.empty());

  const int64 tracker_id = 72;
  // Testing AddToFileIDIndexes
  scoped_ptr<FileTracker> file_tracker =
      test_util::CreateTracker(metadata, tracker_id, &app_root_tracker);

  index()->StoreFileTracker(file_tracker.Pass());
  WriteToDB();
  tracker_ids = index()->GetFileTrackerIDsByParentAndTitle(
      kAppRootTrackerID, "file");
  EXPECT_EQ(2U, tracker_ids.size());
  EXPECT_EQ(tracker_id, tracker_ids.active_tracker());

  ParentIDAndTitle multi_backing = index()->PickMultiBackingFilePath();
  EXPECT_EQ(kAppRootTrackerID, multi_backing.parent_id);
  EXPECT_EQ("file", multi_backing.title);

  // Testing UpdateInFileIDIndexes
  file_tracker =
      test_util::CreateTracker(metadata, tracker_id, &app_root_tracker);
  file_tracker->set_active(false);

  index()->StoreFileTracker(file_tracker.Pass());
  WriteToDB();
  tracker_ids = index()->GetFileTrackerIDsByParentAndTitle(
      kAppRootTrackerID, "file");
  EXPECT_EQ(2U, tracker_ids.size());
  EXPECT_EQ(kInvalidTrackerID, tracker_ids.active_tracker());

  multi_backing = index()->PickMultiBackingFilePath();
  EXPECT_EQ(kAppRootTrackerID, multi_backing.parent_id);
  EXPECT_EQ("file", multi_backing.title);

  file_tracker =
      test_util::CreateTracker(metadata, tracker_id, &app_root_tracker);

  index()->StoreFileTracker(file_tracker.Pass());
  WriteToDB();
  tracker_ids = index()->GetFileTrackerIDsByParentAndTitle(
      kAppRootTrackerID, "file");
  EXPECT_EQ(2U, tracker_ids.size());
  EXPECT_EQ(tracker_id, tracker_ids.active_tracker());

  multi_backing = index()->PickMultiBackingFilePath();
  EXPECT_EQ(kAppRootTrackerID, multi_backing.parent_id);
  EXPECT_EQ("file", multi_backing.title);

  // Testing RemoveFromFileIDIndexes
  index()->RemoveFileTracker(tracker_id);
  WriteToDB();
  tracker_ids = index()->GetFileTrackerIDsByParentAndTitle(
      kAppRootTrackerID, "file");
  EXPECT_EQ(1U, tracker_ids.size());
  EXPECT_EQ(kInvalidTrackerID, tracker_ids.active_tracker());

  multi_backing = index()->PickMultiBackingFilePath();
  EXPECT_EQ(kInvalidTrackerID, multi_backing.parent_id);
  EXPECT_TRUE(multi_backing.title.empty()) << multi_backing.title;
}

TEST_F(MetadataDatabaseIndexOnDiskTest,
       TrackerIDSetByParentIDAndTitleTest_EmptyTitle) {
  CreateTestDatabase(true, nullptr);

  const int64 kFolderTrackerID = 23;
  const int64 kNewFileTrackerID = 42;
  {
    FileTracker app_root_tracker;
    EXPECT_TRUE(index()->GetFileTracker(kAppRootTrackerID, &app_root_tracker));
    scoped_ptr<FileMetadata> folder_metadata =
        test_util::CreateFolderMetadata("folder_id", "folder_name");
    scoped_ptr<FileTracker> folder_tracker =
        test_util::CreateTracker(*folder_metadata, kFolderTrackerID,
                                 &app_root_tracker);
    index()->StoreFileMetadata(folder_metadata.Pass());
    index()->StoreFileTracker(folder_tracker.Pass());
    WriteToDB();
  }

  FileTracker folder_tracker;
  EXPECT_TRUE(index()->GetFileTracker(kFolderTrackerID, &folder_tracker));
  scoped_ptr<FileMetadata> metadata =
      test_util::CreateFileMetadata("file_id2", std::string(), "md5_2");

  // Testing GetFileTrackerIDsByFileID
  TrackerIDSet tracker_ids = index()->GetFileTrackerIDsByParentAndTitle(
      kFolderTrackerID, std::string());
  EXPECT_TRUE(tracker_ids.empty());

  // Testing AddToFileIDIndexes
  scoped_ptr<FileTracker> file_tracker =
      test_util::CreateTracker(*metadata, kNewFileTrackerID, &folder_tracker);

  index()->StoreFileTracker(file_tracker.Pass());
  WriteToDB();
  tracker_ids = index()->GetFileTrackerIDsByParentAndTitle(
      kFolderTrackerID, std::string());
  EXPECT_EQ(1U, tracker_ids.size());
  EXPECT_EQ(kNewFileTrackerID, tracker_ids.active_tracker());

  ParentIDAndTitle multi_backing = index()->PickMultiBackingFilePath();
  EXPECT_EQ(kInvalidTrackerID, multi_backing.parent_id);

  // Testing UpdateInFileIDIndexes
  file_tracker =
      test_util::CreateTracker(*metadata, kNewFileTrackerID, &folder_tracker);

  index()->StoreFileTracker(file_tracker.Pass());
  WriteToDB();
  tracker_ids = index()->GetFileTrackerIDsByParentAndTitle(
      kFolderTrackerID, std::string());
  EXPECT_EQ(1U, tracker_ids.size());
  EXPECT_EQ(kNewFileTrackerID, tracker_ids.active_tracker());

  multi_backing = index()->PickMultiBackingFilePath();
  EXPECT_EQ(kInvalidTrackerID, multi_backing.parent_id);
}

TEST_F(MetadataDatabaseIndexOnDiskTest, TrackerIDSetDetailsTest) {
  CreateTestDatabase(true, nullptr);

  FileTracker app_root;
  EXPECT_TRUE(index()->GetFileTracker(kAppRootTrackerID, &app_root));

  const int64 kFileTrackerID2 = 123;
  const int64 kFileTrackerID3 = 124;
  scoped_ptr<FileMetadata> file_metadata =
      test_util::CreateFileMetadata("file_id2", "file_2", "file_md5_2");
  scoped_ptr<FileTracker> file_tracker =
      test_util::CreateTracker(*file_metadata, kFileTrackerID2, &app_root);
  file_tracker->set_active(false);
  scoped_ptr<FileTracker> file_tracker2 =
      test_util::CreateTracker(*file_metadata, kFileTrackerID3, &app_root);
  file_tracker2->set_active(false);

  // Add 2 trackers related to one file metadata.
  index()->StoreFileMetadata(file_metadata.Pass());
  index()->StoreFileTracker(file_tracker.Pass());
  index()->StoreFileTracker(file_tracker2.Pass());

  TrackerIDSet idset = index()->GetFileTrackerIDsByFileID("file_id2");
  EXPECT_EQ(2U, idset.size());
  EXPECT_FALSE(idset.has_active());

  // Activate one file tracker.
  file_tracker.reset(new FileTracker);
  index()->GetFileTracker(kFileTrackerID2, file_tracker.get());
  file_tracker->set_active(true);
  index()->StoreFileTracker(file_tracker.Pass());

  idset = index()->GetFileTrackerIDsByFileID("file_id2");
  EXPECT_EQ(2U, idset.size());
  EXPECT_TRUE(idset.has_active());
  EXPECT_EQ(kFileTrackerID2, idset.active_tracker());
}

TEST_F(MetadataDatabaseIndexOnDiskTest, DirtyTrackersTest) {
  CreateTestDatabase(true, nullptr);

  // Testing public methods
  EXPECT_EQ(1U, index()->CountDirtyTracker());
  EXPECT_FALSE(index()->HasDemotedDirtyTracker());
  EXPECT_EQ(kPlaceholderTrackerID, index()->PickDirtyTracker());
  index()->DemoteDirtyTracker(kPlaceholderTrackerID);
  WriteToDB();
  EXPECT_TRUE(index()->HasDemotedDirtyTracker());
  EXPECT_EQ(0U, index()->CountDirtyTracker());

  const int64 tracker_id = 13;
  scoped_ptr<FileTracker> app_root_tracker(new FileTracker);
  index()->GetFileTracker(kAppRootTrackerID, app_root_tracker.get());

  // Testing AddDirtyTrackerIndexes
  scoped_ptr<FileTracker> tracker =
      test_util::CreatePlaceholderTracker("placeholder",
                                          tracker_id,
                                          app_root_tracker.get());
  index()->StoreFileTracker(tracker.Pass());
  WriteToDB();
  EXPECT_EQ(1U, index()->CountDirtyTracker());
  EXPECT_EQ(tracker_id, index()->PickDirtyTracker());

  // Testing UpdateDirtyTrackerIndexes
  tracker = test_util::CreatePlaceholderTracker("placeholder",
                                                tracker_id,
                                                app_root_tracker.get());
  tracker->set_dirty(false);
  index()->StoreFileTracker(tracker.Pass());
  WriteToDB();
  EXPECT_EQ(0U, index()->CountDirtyTracker());
  EXPECT_EQ(kInvalidTrackerID, index()->PickDirtyTracker());

  tracker = test_util::CreatePlaceholderTracker("placeholder",
                                                tracker_id,
                                                app_root_tracker.get());
  index()->StoreFileTracker(tracker.Pass());
  WriteToDB();
  EXPECT_EQ(1U, index()->CountDirtyTracker());
  EXPECT_EQ(tracker_id, index()->PickDirtyTracker());

  // Testing RemoveFromDirtyTrackerIndexes
  index()->RemoveFileTracker(tracker_id);
  WriteToDB();
  EXPECT_EQ(0U, index()->CountDirtyTracker());
  EXPECT_EQ(kInvalidTrackerID, index()->PickDirtyTracker());

  // Demoted trackers
  EXPECT_TRUE(index()->HasDemotedDirtyTracker());
  EXPECT_TRUE(index()->PromoteDemotedDirtyTrackers());
  EXPECT_FALSE(index()->HasDemotedDirtyTracker());
}

}  // namespace drive_backend
}  // namespace sync_file_system
