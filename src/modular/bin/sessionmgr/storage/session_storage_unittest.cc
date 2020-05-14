// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/storage/session_storage.h"

#include <lib/fidl/cpp/optional.h>
#include <lib/gtest/real_loop_fixture.h>

#include <memory>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/lib/fsl/vmo/strings.h"
#include "src/modular/bin/sessionmgr/testing/annotations_matchers.h"
#include "src/modular/lib/async/cpp/future.h"
#include "src/modular/lib/fidl/array_to_string.h"
#include "zircon/system/public/zircon/errors.h"

namespace modular {
namespace {

using ::testing::ByRef;

class SessionStorageTest : public gtest::RealLoopFixture {
 protected:
  std::unique_ptr<SessionStorage> CreateStorage() {
    return std::make_unique<SessionStorage>();
  }
};

TEST_F(SessionStorageTest, Create_VerifyData) {
  // Create a single story, and verify that the data we have stored about it is
  // correct.
  auto storage = CreateStorage();

  std::vector<fuchsia::modular::Annotation> annotations{};
  fuchsia::modular::AnnotationValue annotation_value;
  annotation_value.set_text("test_annotation_value");
  auto annotation = fuchsia::modular::Annotation{
      .key = "test_annotation_key", .value = fidl::MakeOptional(std::move(annotation_value))};
  annotations.push_back(fidl::Clone(annotation));

  auto story_name = storage->CreateStory("story_name", std::move(annotations));

  // Get the StoryData for this story.
  auto future_data = storage->GetStoryData(story_name);
  auto done = false;
  fuchsia::modular::internal::StoryData cached_data;
  future_data->Then([&](fuchsia::modular::internal::StoryDataPtr data) {
    ASSERT_TRUE(data);

    EXPECT_EQ("story_name", data->story_name());
    EXPECT_EQ(story_name, data->story_info().id());

    EXPECT_TRUE(data->story_info().has_annotations());
    EXPECT_EQ(1u, data->story_info().annotations().size());
    EXPECT_THAT(data->story_info().annotations().at(0),
                annotations::AnnotationEq(ByRef(annotation)));

    done = true;

    cached_data = std::move(*data);
  });
  RunLoopUntil([&] { return done; });

  // Get the StoryData again, but this time by its name.
  future_data = storage->GetStoryData("story_name");
  done = false;
  future_data->Then([&](fuchsia::modular::internal::StoryDataPtr data) {
    ASSERT_TRUE(data);
    ASSERT_TRUE(fidl::Equals(cached_data, *data));
    done = true;
  });
  RunLoopUntil([&] { return done; });

  // Verify that GetAllStoryData() also returns the same information.
  fidl::VectorPtr<fuchsia::modular::internal::StoryData> all_data;
  auto future_all_data = storage->GetAllStoryData();
  future_all_data->Then([&](std::vector<fuchsia::modular::internal::StoryData> data) {
    all_data.emplace(std::move(data));
  });
  RunLoopUntil([&] { return all_data.has_value(); });

  EXPECT_EQ(1u, all_data->size());
  EXPECT_TRUE(fidl::Equals(cached_data, all_data->at(0)));
}


TEST_F(SessionStorageTest, Create_VerifyData_NoAnntations) {
  // Create a single story with no annotations, and verify that the data we have stored about it is
  // correct.
  auto storage = CreateStorage();

  storage->CreateStory("story_name", {});
  // Get the StoryData for this story.
  auto future_data = storage->GetStoryData("story_name");
  bool done = false;
  future_data->Then([&](fuchsia::modular::internal::StoryDataPtr data) {
    ASSERT_TRUE(data);

    EXPECT_EQ("story_name", data->story_name());
    EXPECT_EQ("story_name", data->story_info().id());

    EXPECT_TRUE(data->story_info().has_annotations());
    EXPECT_EQ(0u, data->story_info().annotations().size());

    done = true;
  });
  RunLoopUntil([&] { return done; });
}

TEST_F(SessionStorageTest, CreateGetAllDelete) {
  // Create a single story, call GetAllStoryData() to show that it was created,
  // and then delete it.
  //
  // Since the implementation has switched from an asynchronous one to a
  // synchronous one in asynchronous clothing, don't rely on Future ordering for
  // consistency.  Rely only on function call ordering.  We'll switch the
  // interface to be blocking in a future commit.
  auto storage = CreateStorage();
  storage->CreateStory("story_name", /*annotations=*/{});

  auto future_all_data = storage->GetAllStoryData();
  fidl::VectorPtr<fuchsia::modular::internal::StoryData> all_data;
  future_all_data->Then([&](std::vector<fuchsia::modular::internal::StoryData> data) {
    all_data.emplace(std::move(data));
  });

  RunLoopUntil([&] { return all_data.has_value(); });

  // Then, delete it.
  storage->DeleteStory("story_name");

  // Given the ordering, we expect the story we created to show up.
  EXPECT_EQ(1u, all_data->size());

  // But if we get all data again, we should see no stories.
  future_all_data = storage->GetAllStoryData();
  all_data.reset();
  future_all_data->Then([&](std::vector<fuchsia::modular::internal::StoryData> data) {
    all_data.emplace(std::move(data));
  });
  RunLoopUntil([&] { return all_data.has_value(); });
  EXPECT_EQ(0u, all_data->size());
}

TEST_F(SessionStorageTest, CreateMultipleAndDeleteOne) {
  // Create two stories.
  //
  // * Their ids should be different.
  // * They should get different names.
  // * If we GetAllStoryData() we should see both of them.
  auto storage = CreateStorage();

  auto story1_name = storage->CreateStory("story1", /*annotations=*/{});
  auto story2_name = storage->CreateStory("story2", /*annotations=*/{});

  EXPECT_NE(story1_name, story2_name);

  auto future_all_data = storage->GetAllStoryData();
  fidl::VectorPtr<fuchsia::modular::internal::StoryData> all_data;
  future_all_data->Then([&](std::vector<fuchsia::modular::internal::StoryData> data) {
    all_data.emplace(std::move(data));
  });
  RunLoopUntil([&] { return all_data.has_value(); });

  EXPECT_EQ(2u, all_data->size());

  // Now delete one of them, and we should see that GetAllStoryData() only
  // returns one entry.
  storage->DeleteStory("story1");

  future_all_data = storage->GetAllStoryData();
  all_data.reset();
  future_all_data->Then([&](std::vector<fuchsia::modular::internal::StoryData> data) {
    all_data.emplace(std::move(data));
  });
  RunLoopUntil([&] { return all_data.has_value(); });
  EXPECT_EQ(1u, all_data->size());

  // If we try to get the story by id, or by name, we expect both to return
  // null.
  auto future_data = storage->GetStoryData(story1_name);
  auto done = false;
  future_data->Then([&](fuchsia::modular::internal::StoryDataPtr data) {
    EXPECT_TRUE(data == nullptr);
    done = true;
  });

  future_data = storage->GetStoryData("story1");
  done = false;
  future_data->Then([&](fuchsia::modular::internal::StoryDataPtr data) {
    EXPECT_TRUE(data == nullptr);
    done = true;
  });
}

TEST_F(SessionStorageTest, CreateSameStoryOnlyOnce) {
  // Call CreateStory twice with the same story name, but with annotations only in the first call.
  // Both calls should succeed, and the second call should be a no-op:
  //
  //   * The story should only be created once.
  //   * The second call should return the same story name as the first.
  //   * The final StoryData should contain annotations from the first call.
  auto storage = CreateStorage();

  // Only the first CreateStory call has annotations.
  std::vector<fuchsia::modular::Annotation> annotations{};
  fuchsia::modular::AnnotationValue annotation_value;
  annotation_value.set_text("test_annotation_value");
  auto annotation = fuchsia::modular::Annotation{
      .key = "test_annotation_key", .value = fidl::MakeOptional(std::move(annotation_value))};
  annotations.push_back(fidl::Clone(annotation));

  auto story_first_name = storage->CreateStory("story", std::move(annotations));
  auto story_second_name = storage->CreateStory("story", /*annotations=*/{});

  // Both calls should return the same name because they refer to the same story.
  EXPECT_EQ(story_first_name, story_second_name);

  // Only one story should have been created.
  auto future_all_data = storage->GetAllStoryData();
  fidl::VectorPtr<fuchsia::modular::internal::StoryData> all_data;
  future_all_data->Then([&](std::vector<fuchsia::modular::internal::StoryData> data) {
    all_data.emplace(std::move(data));
  });
  RunLoopUntil([&] { return all_data.has_value(); });

  EXPECT_EQ(1u, all_data->size());

  // The story should have the annotation from the first call to CreateStory.
  const auto& story_info = all_data->at(0).story_info();
  EXPECT_TRUE(story_info.has_annotations());
  EXPECT_EQ(1u, story_info.annotations().size());
  EXPECT_THAT(story_info.annotations().at(0), annotations::AnnotationEq(ByRef(annotation)));
}

TEST_F(SessionStorageTest, UpdateLastFocusedTimestamp) {
  auto storage = CreateStorage();
  auto story_name = storage->CreateStory("story", {});

  storage->UpdateLastFocusedTimestamp(story_name, 10);
  auto future_data = storage->GetStoryData(story_name);
  bool done{};
  future_data->Then([&](fuchsia::modular::internal::StoryDataPtr data) {
    EXPECT_EQ(10, data->story_info().last_focus_time());
    done = true;
  });
  RunLoopUntil([&] { return done; });
}

TEST_F(SessionStorageTest, ObserveCreateUpdateDelete) {
  auto storage = CreateStorage();

  bool updated{};
  fidl::StringPtr updated_story_name;
  fuchsia::modular::internal::StoryData updated_story_data;
  storage->set_on_story_updated(
      [&](fidl::StringPtr story_name, fuchsia::modular::internal::StoryData story_data) {
        updated_story_name = std::move(story_name);
        updated_story_data = std::move(story_data);
        updated = true;
      });

  bool deleted{};
  fidl::StringPtr deleted_story_name;
  storage->set_on_story_deleted([&](fidl::StringPtr story_name) {
    deleted_story_name = std::move(story_name);
    deleted = true;
  });

  auto created_story_name = storage->CreateStory("story", {});
  RunLoopUntil([&] { return updated; });
  EXPECT_EQ(created_story_name, updated_story_name.value());
  EXPECT_EQ(created_story_name, updated_story_data.story_info().id());

  // Update something and see a new notification.
  updated = false;
  storage->UpdateLastFocusedTimestamp(created_story_name, 42);
  RunLoopUntil([&] { return updated; });
  EXPECT_EQ(created_story_name, updated_story_name.value());
  EXPECT_EQ(42, updated_story_data.story_info().last_focus_time());

  // Delete the story and expect to see a notification.
  storage->DeleteStory(created_story_name);
  RunLoopUntil([&] { return deleted; });
  EXPECT_EQ(created_story_name, deleted_story_name.value());

  // Once a story is already deleted, do not expect another
  // notification.
  deleted = false;
  storage->DeleteStory(created_story_name);
  EXPECT_EQ(false, deleted);
}

TEST_F(SessionStorageTest, GetStoryStorage) {
  auto storage = CreateStorage();
  auto story_name = storage->CreateStory("story", {});
  EXPECT_NE(nullptr, storage->GetStoryStorage(story_name));
}

TEST_F(SessionStorageTest, GetStoryStorageNoStory) {
  auto storage = CreateStorage();
  storage->CreateStory("story", {});
  EXPECT_EQ(nullptr, storage->GetStoryStorage("fake"));
}

}  // namespace
}  // namespace modular
