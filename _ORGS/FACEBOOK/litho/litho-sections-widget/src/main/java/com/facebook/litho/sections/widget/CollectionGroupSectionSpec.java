/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.facebook.litho.sections.widget;

import com.facebook.litho.ComponentContext;
import com.facebook.litho.annotations.Prop;
import com.facebook.litho.sections.Children;
import com.facebook.litho.sections.SectionContext;
import com.facebook.litho.sections.annotations.GroupSectionSpec;
import com.facebook.litho.sections.annotations.OnCreateChildren;
import com.facebook.litho.sections.annotations.OnDataBound;
import com.facebook.litho.sections.annotations.OnRefresh;
import com.facebook.litho.sections.annotations.OnViewportChanged;
import kotlin.jvm.functions.Function0;
import kotlin.jvm.functions.Function1;
import kotlin.jvm.functions.Function6;

@GroupSectionSpec
class CollectionGroupSectionSpec {

  @OnCreateChildren
  static Children onCreateChildren(SectionContext c, @Prop Children.Builder childrenBuilder) {
    return childrenBuilder.build();
  }

  @OnDataBound
  static void onDataBound(
      SectionContext c,
      @Prop(optional = true) Function1<ComponentContext, kotlin.Unit> onDataBound) {
    if (onDataBound != null) {
      onDataBound.invoke(c);
    }
  }

  @OnViewportChanged
  static void onViewportChanged(
      final SectionContext c,
      int firstVisibleIndex,
      int lastVisibleIndex,
      int totalCount,
      int firstFullyVisibleIndex,
      int lastFullyVisibleIndex,
      @Prop(optional = true)
          Function6<ComponentContext, Integer, Integer, Integer, Integer, Integer, kotlin.Unit>
              onViewportChanged) {
    if (onViewportChanged != null) {
      onViewportChanged.invoke(
          c,
          firstVisibleIndex,
          lastVisibleIndex,
          totalCount,
          firstFullyVisibleIndex,
          lastFullyVisibleIndex);
    }
  }

  @OnRefresh
  static void onRefresh(
      SectionContext c, @Prop(optional = true) Function0<kotlin.Unit> onPullToRefresh) {
    onPullToRefresh.invoke();
  }
}
