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

package com.facebook.litho;

import static androidx.test.core.app.ApplicationProvider.getApplicationContext;
import static com.facebook.litho.LithoKeyTestingUtil.getScopedComponentInfos;
import static org.assertj.core.api.Assertions.assertThat;

import android.view.View;
import com.facebook.litho.annotations.OnCreateLayout;
import com.facebook.litho.config.ComponentsConfiguration;
import com.facebook.litho.config.TempComponentsConfigurations;
import com.facebook.litho.testing.TestViewComponent;
import com.facebook.litho.testing.inlinelayoutspec.InlineLayoutSpec;
import com.facebook.litho.testing.testrunner.LithoTestRunner;
import com.facebook.litho.widget.CardClip;
import com.facebook.litho.widget.Text;
import java.util.List;
import java.util.Map;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(LithoTestRunner.class)
public class LegacyStatelessComponentGlobalKeyTest {

  private ComponentContext mContext;

  @Before
  public void setup() {
    TempComponentsConfigurations.setShouldAddHostViewForRootComponent(false);
    ComponentsConfiguration.useStatelessComponent = true;
    mContext = new ComponentContext(getApplicationContext());
  }

  @Test
  public void testMultipleChildrenComponentKey() {
    final Component component = getMultipleChildrenComponent();

    int layoutSpecId = component.getTypeId();
    int nestedLayoutSpecId = layoutSpecId - 1;

    final Component column = Column.create(mContext).build();
    final int columnSpecId = column.getTypeId();

    final LithoView lithoView = getLithoView(component);
    final LayoutStateContext layoutStateContext =
        lithoView.getComponentTree().getLayoutStateContext();
    final Map<String, List<ScopedComponentInfo>> globalKeysInfo =
        getScopedComponentInfos(lithoView);

    // Text
    final String expectedGlobalKeyText =
        ComponentKeyUtils.getKeyWithSeparatorForTest(layoutSpecId, columnSpecId, "$[Text2]");
    final ScopedComponentInfo scopedComponentInfoText = globalKeysInfo.get("Text").get(0);
    assertThat(scopedComponentInfoText.getContext().getGlobalKey())
        .isEqualTo(expectedGlobalKeyText);
    // TestViewComponent in child layout
    final String expectedGlobalKeyTestViewComponent =
        ComponentKeyUtils.getKeyWithSeparatorForTest(
            layoutSpecId, columnSpecId, nestedLayoutSpecId, columnSpecId, "$[TestViewComponent1]");
    final ScopedComponentInfo scopedComponentInfoTestViewComponent =
        globalKeysInfo.get("TestViewComponent").get(0);
    assertThat(scopedComponentInfoTestViewComponent.getContext().getGlobalKey())
        .isEqualTo(expectedGlobalKeyTestViewComponent);
    // CardClip in child
    final String expectedGlobalKeyCardClip =
        ComponentKeyUtils.getKeyWithSeparatorForTest(
            layoutSpecId,
            columnSpecId,
            nestedLayoutSpecId,
            columnSpecId,
            columnSpecId,
            "$[CardClip1]");
    final ScopedComponentInfo scopedComponentInfoCardClip = globalKeysInfo.get("CardClip").get(0);
    assertThat(scopedComponentInfoCardClip.getContext().getGlobalKey())
        .isEqualTo(expectedGlobalKeyCardClip);

    // Text in child
    final String expectedGlobalKeyTextChild =
        ComponentKeyUtils.getKeyWithSeparatorForTest(
            layoutSpecId, columnSpecId, nestedLayoutSpecId, columnSpecId, "$[Text1]");
    final ScopedComponentInfo scopedComponentInfoTextChild = globalKeysInfo.get("Text").get(1);
    assertThat(scopedComponentInfoTextChild.getContext().getGlobalKey())
        .isEqualTo(expectedGlobalKeyTextChild);

    // CardClip
    final String expectedGlobalKeyCardClip2 =
        ComponentKeyUtils.getKeyWithSeparatorForTest(
            layoutSpecId, columnSpecId, columnSpecId, "$[CardClip2]");
    final ScopedComponentInfo scopedComponentInfoCardClip2 = globalKeysInfo.get("CardClip").get(1);
    assertThat(scopedComponentInfoCardClip2.getContext().getGlobalKey())
        .isEqualTo(expectedGlobalKeyCardClip2);

    // TestViewComponent
    final String expectedGlobalKeyTestViewComponent2 =
        ComponentKeyUtils.getKeyWithSeparatorForTest(
            layoutSpecId, columnSpecId, "$[TestViewComponent2]");
    final ScopedComponentInfo scopedComponentInfoTestViewComponent2 =
        globalKeysInfo.get("TestViewComponent").get(1);
    assertThat(scopedComponentInfoTestViewComponent2.getContext().getGlobalKey())
        .isEqualTo(expectedGlobalKeyTestViewComponent2);
  }

  private LithoView getLithoView(Component component) {
    LithoView lithoView = new LithoView(mContext);
    lithoView.setComponent(component);
    lithoView.measure(
        View.MeasureSpec.makeMeasureSpec(640, View.MeasureSpec.UNSPECIFIED),
        View.MeasureSpec.makeMeasureSpec(480, View.MeasureSpec.UNSPECIFIED));
    lithoView.layout(0, 0, lithoView.getMeasuredWidth(), lithoView.getMeasuredHeight());
    return lithoView;
  }

  private static Component getMultipleChildrenComponent() {
    final int color = 0xFFFF0000;
    final Component testGlobalKeyChildComponent =
        new InlineLayoutSpec() {

          @Override
          @OnCreateLayout
          protected Component onCreateLayout(ComponentContext c) {

            return Column.create(c)
                .child(
                    TestViewComponent.create(c)
                        .widthDip(10)
                        .heightDip(10)
                        .key("[TestViewComponent1]"))
                .child(
                    Column.create(c)
                        .backgroundColor(color)
                        .child(CardClip.create(c).widthDip(10).heightDip(10).key("[CardClip1]")))
                .child(Text.create(c).text("Test").widthDip(10).heightDip(10).key("[Text1]"))
                .build();
          }
        };

    final Component testGlobalKeyChild =
        new InlineLayoutSpec() {

          @Override
          @OnCreateLayout
          protected Component onCreateLayout(ComponentContext c) {

            return Column.create(c)
                .child(Text.create(c).text("test").widthDip(10).heightDip(10).key("[Text2]"))
                .child(testGlobalKeyChildComponent)
                .child(
                    Column.create(c)
                        .backgroundColor(color)
                        .child(CardClip.create(c).widthDip(10).heightDip(10).key("[CardClip2]")))
                .child(
                    TestViewComponent.create(c)
                        .widthDip(10)
                        .heightDip(10)
                        .key("[TestViewComponent2]"))
                .build();
          }
        };

    return testGlobalKeyChild;
  }

  @After
  public void restoreConfiguration() {
    TempComponentsConfigurations.restoreShouldAddHostViewForRootComponent();
  }
}
