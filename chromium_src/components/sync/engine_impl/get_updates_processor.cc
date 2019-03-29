#include "components/sync/engine_impl/get_updates_processor.h"

namespace syncer {
namespace {
SyncerError ApplyBraveRecords(sync_pb::ClientToServerResponse*, ModelTypeSet*,
                              std::unique_ptr<brave_sync::RecordsList>);
}   // namespace
}   // namespace syncer
#include "../../../../../components/sync/engine_impl/get_updates_processor.cc"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "brave/components/brave_sync/jslib_messages.h"
#include "components/sync/syncable/syncable_proto_util.h"
#include "components/sync/engine_impl/loopback_server/loopback_server_entity.h"
#include "url/gurl.h"

namespace syncer {
namespace {

using brave_sync::jslib::Bookmark;
using brave_sync::jslib::SyncRecord;
using syncable::Id;
static const char kBookmarkBarTag[] = "bookmark_bar";
static const char kOtherBookmarksTag[] = "other_bookmarks";
static const char kBookmarkBarFolderName[] = "Bookmark Bar";
static const char kOtherBookmarksFolderName[] = "Other Bookmarks";
// The parent tag for children of the root entity. Entities with this parent are
// referred to as top level enities.
static const char kRootParentTag[] = "0";

uint64_t GetIndexByOrder(const std::string& record_order) {
  uint64_t index = 0;
  char last_ch = record_order.back();
  bool result = base::StringToUint64(std::string(&last_ch), &index);
  --index;
  DCHECK(index >= 0);
  DCHECK(result);
  return index;
}

void AddBookmarkSpecifics(sync_pb::EntitySpecifics* specifics,
                          const Bookmark& bookmark) {
  DCHECK(specifics);
  sync_pb::BookmarkSpecifics* bm_specifics = specifics->mutable_bookmark();
  bm_specifics->set_url(bookmark.site.location);
  bm_specifics->set_title(bookmark.site.title);
  bm_specifics->set_creation_time_us(
    TimeToProtoTime(bookmark.site.creationTime));
      // base::Time::Now().ToDeltaSinceWindowsEpoch().InMicroseconds());
  sync_pb::MetaInfo* meta_info = bm_specifics->add_meta_info();
  meta_info->set_key("order");
  meta_info->set_value(bookmark.order);
}

void ExtractBookmarkMeta(sync_pb::SyncEntity* entity,
                         const Bookmark& bookmark) {
  for (const auto metaInfo : bookmark.metaInfo) {
    if (metaInfo.key == "originator_cache_guid")
      entity->set_originator_cache_guid(metaInfo.value);
    else if (metaInfo.key == "originator_client_item_id")
      entity->set_originator_client_item_id(metaInfo.value);
    else if (metaInfo.key == "version") {
      int64_t version;
      bool result = base::StringToInt64(metaInfo.value, &version);
      // TODO(darkdh): not a good place to increase version
      entity->set_version(version + 1);
      DCHECK(result);
    }
  }
}

void AddRootForType(sync_pb::SyncEntity* entity, ModelType type) {
  DCHECK(entity);
  sync_pb::EntitySpecifics specifics;
  AddDefaultFieldValue(type, &specifics);
  std::string server_tag = ModelTypeToRootTag(type);
  std::string name = syncer::ModelTypeToString(type);
  std::string id = LoopbackServerEntity::GetTopLevelId(type);
  entity->set_server_defined_unique_tag(server_tag);
  entity->set_deleted(false);
  entity->set_id_string(id);
  entity->set_parent_id_string(kRootParentTag);
  entity->set_name(name);
  entity->set_version(1);
  entity->set_folder(true);
  entity->mutable_specifics()->CopyFrom(specifics);
}

void AddPermanentNode(sync_pb::SyncEntity* entity, const std::string& name, const std::string& tag) {
  DCHECK(entity);
  sync_pb::EntitySpecifics specifics;
  AddDefaultFieldValue(BOOKMARKS, &specifics);
  std::string parent = ModelTypeToRootTag(BOOKMARKS);
  std::string id = tag;
  std::string parent_id = LoopbackServerEntity::CreateId(BOOKMARKS, parent);
  entity->set_server_defined_unique_tag(tag);
  entity->set_deleted(false);
  entity->set_id_string(id);
  entity->set_parent_id_string(parent_id);
  entity->set_name(name);
  entity->set_folder(true);
  entity->set_version(1);
  entity->mutable_specifics()->CopyFrom(specifics);
}

void AddBookmarkNode(sync_pb::SyncEntity* entity, const SyncRecord* record) {
  DCHECK(entity);
  DCHECK(record);
  DCHECK(record->has_bookmark());
  DCHECK(!record->objectId.empty());

  auto bookmark_record = record->GetBookmark();

  sync_pb::EntitySpecifics specifics;
  AddDefaultFieldValue(BOOKMARKS, &specifics);
  if (record->action == SyncRecord::Action::A_UPDATE) {
    // TODO(darkdh): requires SyncEntity.version to be set correctly
  } else if (record->action == SyncRecord::Action::A_DELETE) {
    // TODO(darkdh): make bookmark specific empty
  } else if (record->action == SyncRecord::Action::A_CREATE) {
    entity->set_id_string(record->objectId);
    if (!bookmark_record.parentFolderObjectId.empty())
      entity->set_parent_id_string(bookmark_record.parentFolderObjectId);
    else if (!bookmark_record.hideInToolbar)
      entity->set_parent_id_string(std::string(kBookmarkBarTag));
    else
      entity->set_parent_id_string(std::string(kOtherBookmarksTag));
    entity->set_non_unique_name(bookmark_record.site.title);
    entity->set_folder(bookmark_record.isFolder);
    entity->set_deleted(false);

    ExtractBookmarkMeta(entity, bookmark_record);

    // TODO(darkdh): migrate to UniquePosition
    entity->set_position_in_parent(GetIndexByOrder(bookmark_record.order));
    entity->set_ctime(TimeToProtoTime(base::Time::Now()));
    entity->set_mtime(TimeToProtoTime(base::Time::Now()));
    AddBookmarkSpecifics(&specifics, bookmark_record);
    entity->mutable_specifics()->CopyFrom(specifics);
  }
}

void ConstructUpdateResponse(sync_pb::GetUpdatesResponse* gu_response,
                             ModelTypeSet* request_types,
                             std::unique_ptr<RecordsList> records) {
  DCHECK(gu_response);
  DCHECK(request_types);
  for (ModelType type : *request_types) {
    sync_pb::DataTypeProgressMarker* marker =
      gu_response->add_new_progress_marker();
    marker->set_data_type_id(GetSpecificsFieldNumberFromModelType(type));
    marker->set_token("token");
    sync_pb::DataTypeContext* context = gu_response->add_context_mutations();
    context->set_data_type_id(GetSpecificsFieldNumberFromModelType(type));
    context->set_version(1);
    context->set_context("context");
    if (type == BOOKMARKS) {
      google::protobuf::RepeatedPtrField<sync_pb::SyncEntity> entities;
      AddRootForType(entities.Add(), BOOKMARKS);
      AddPermanentNode(entities.Add(), kBookmarkBarFolderName, kBookmarkBarTag);
      AddPermanentNode(entities.Add(), kOtherBookmarksFolderName,
                       kOtherBookmarksTag);
      if (records) {
        for (const auto& record : *records.get()) {
          AddBookmarkNode(entities.Add(), record.get());
        }
      }
      std::copy(entities.begin(), entities.end(),
                RepeatedPtrFieldBackInserter(gu_response->mutable_entries()));
    }
    gu_response->set_changes_remaining(0);
  }
}

SyncerError ApplyBraveRecords(sync_pb::ClientToServerResponse* update_response,
                              ModelTypeSet* request_types,
                              std::unique_ptr<RecordsList> records) {
  DCHECK(update_response);
  DCHECK(request_types);
  sync_pb::GetUpdatesResponse* gu_response = new sync_pb::GetUpdatesResponse();
  ConstructUpdateResponse(gu_response, request_types, std::move(records));
  update_response->set_allocated_get_updates(gu_response);
  return SyncerError(SyncerError::SYNCER_OK);
}

}   // namespace

void GetUpdatesProcessor::AddBraveRecords(
    std::unique_ptr<RecordsList> records) {
  brave_records_ = std::move(records);
}

}   // namespace syncer
