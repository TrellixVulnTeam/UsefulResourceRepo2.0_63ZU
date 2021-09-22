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

package com.facebook.litho.widget;

import android.graphics.Color;
import com.facebook.litho.Column;
import com.facebook.litho.Component;
import com.facebook.litho.ComponentContext;
import com.facebook.litho.LifecycleStep;
import com.facebook.litho.LifecycleStep.StepInfo;
import com.facebook.litho.Row;
import com.facebook.litho.SizeSpec;
import com.facebook.litho.StateValue;
import com.facebook.litho.Wrapper;
import com.facebook.litho.annotations.LayoutSpec;
import com.facebook.litho.annotations.OnCreateInitialState;
import com.facebook.litho.annotations.OnCreateLayoutWithSizeSpec;
import com.facebook.litho.annotations.OnShouldCreateLayoutWithNewSizeSpec;
import com.facebook.litho.annotations.Prop;
import com.facebook.litho.annotations.PropDefault;
import com.facebook.litho.annotations.State;
import java.util.List;

@LayoutSpec
class LayoutWithSizeSpecLifecycleTesterSpec {

  static final @PropDefault boolean causeYogaRemeasure = false;

  @OnCreateInitialState
  static void onCreateInitialState(
      ComponentContext c, StateValue<String> state, @Prop List<LifecycleStep.StepInfo> steps) {
    steps.add(new StepInfo(LifecycleStep.ON_CREATE_INITIAL_STATE));
    state.set("hello world");
  }

  @OnCreateLayoutWithSizeSpec
  static Component onCreateLayout(
      ComponentContext c,
      int w,
      int h,
      @Prop List<LifecycleStep.StepInfo> steps,
      @Prop(optional = true) Component body,
      @Prop(optional = true) boolean causeYogaRemeasure,
      @State String state) {

    steps.add(new StepInfo(LifecycleStep.ON_CREATE_LAYOUT_WITH_SIZE_SPEC));
    if (state == null) {
      throw new IllegalStateException("OnCreateLayout called without initialised state.");
    }
    return Column.create(c)
        .heightPx(100)
        .child(
            Wrapper.create(c)
                .delegate(
                    body != null
                        ? body
                        : SolidColor.create(c)
                            .color(Color.BLACK)
                            .widthPx(SizeSpec.getSize(w))
                            .build()))
        .child(causeYogaRemeasure ? Row.create(c).heightPx(SizeSpec.getSize(h)) : null)
        .build();
  }

  @OnShouldCreateLayoutWithNewSizeSpec
  static boolean onShouldCreateLayoutWithNewSizeSpec(
      ComponentContext c,
      int newWidthSpec,
      int newHeightSpec,
      @Prop(optional = true) Boolean shouldReusePreviousLayout) {
    if (shouldReusePreviousLayout != null) {
      return shouldReusePreviousLayout;
    } else {
      return false;
    }
  }
}
