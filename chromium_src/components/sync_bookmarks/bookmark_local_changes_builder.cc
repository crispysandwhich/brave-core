namespace bookmarks {
class BookmarkModel;
class BookmarkNode;
}   // namespace bookmarks

namespace sync_pb {
class BookmarkSpecifics;
}   // namespace sync_pb

namespace sync_bookmarks {
class SyncedBookmarkTracker;
namespace {

void AddBraveMetaInfo(sync_pb::BookmarkSpecifics* bm_specifics,
                      const bookmarks::BookmarkNode* node,
                      const SyncedBookmarkTracker* bookmark_tracker,
                      bookmarks::BookmarkModel* bookmark_model);

}   // namespace
}   // namespace sync_bookmarks

#include "../../../../components/sync_bookmarks/bookmark_local_changes_builder.cc"
namespace sync_bookmarks {
namespace {

void GetPrevObjectId(const bookmarks::BookmarkNode* parent,
                     const SyncedBookmarkTracker* bookmark_tracker,
                     int index,
                     std::string* prev_object_id) {
  DCHECK_GE(index, 0);
  auto* prev_node = index == 0 ?
    nullptr :
    parent->GetChild(index - 1);

  if (prev_node) {
    const SyncedBookmarkTracker::Entity* prev_entity =
        bookmark_tracker->GetEntityForBookmarkNode(prev_node);
    *prev_object_id = prev_entity->metadata()->server_id();
  }
}

void GetOrder(const bookmarks::BookmarkNode* parent,
              int index,
              std::string* prev_order,
              std::string* next_order,
              std::string* parent_order) {
  DCHECK_GE(index, 0);
  auto* prev_node = index == 0 ?
    nullptr :
    parent->GetChild(index - 1);
  auto* next_node = index == parent->child_count() - 1 ?
    nullptr :
    parent->GetChild(index + 1);

  if (prev_node)
    prev_node->GetMetaInfo("order", prev_order);

  if (next_node)
    next_node->GetMetaInfo("order", next_order);

  parent->GetMetaInfo("order", parent_order);
}

void AddMetaInfo(sync_pb::BookmarkSpecifics* bm_specifics,
             const std::string& key,
             const std::string& value) {
  sync_pb::MetaInfo* meta_info = bm_specifics->add_meta_info();
  meta_info->set_key(key);
  meta_info->set_value(value);
}

void AddBraveMetaInfo(sync_pb::BookmarkSpecifics* bm_specifics,
                      const bookmarks::BookmarkNode* node,
                      const SyncedBookmarkTracker* bookmark_tracker,
                      bookmarks::BookmarkModel* bookmark_model) {
  std::string prev_object_id;
  int index = node->parent()->GetIndexOf(node);
  GetPrevObjectId(node->parent(), bookmark_tracker, index, &prev_object_id);
  AddMetaInfo(bm_specifics, "prevObjectId", prev_object_id);

  std::string parent_order;
  std::string prev_order;
  std::string next_order;
  GetOrder(node->parent(), index, &prev_order, &next_order, &parent_order);
  AddMetaInfo(bm_specifics, "prevOrder", prev_order);
  AddMetaInfo(bm_specifics, "nextOrder", next_order);
  AddMetaInfo(bm_specifics, "parentOrder", parent_order);
}

}   // namespace
}   // namespace sync_bookmarks
