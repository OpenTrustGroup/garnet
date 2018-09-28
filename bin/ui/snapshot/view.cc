// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/snapshot/view.h"

#include "garnet/lib/ui/gfx/resources/snapshot/version.h"
#include "lib/fsl/vmo/vector.h"
#include "lib/fxl/logging.h"
#include "lib/ui/scenic/cpp/host_memory.h"

namespace snapshot {

View::View(async::Loop* loop, component::StartupContext* startup_context,
           ::fuchsia::ui::viewsv1::ViewManagerPtr view_manager,
           fidl::InterfaceRequest<::fuchsia::ui::viewsv1token::ViewOwner>
               view_owner_request)
    : BaseView(std::move(view_manager), std::move(view_owner_request),
               "Snapshot View") {
  startup_context->outgoing().AddPublicService(
      loader_bindings_.GetHandler(this));
}

void View::Load(::fuchsia::mem::Buffer payload) {
  std::vector<uint8_t> data;
  if (!fsl::VectorFromVmo(payload, &data)) {
    FXL_LOG(ERROR) << "VectorFromVmo failed";
    return;
  }

  using SnapshotData = scenic_impl::gfx::SnapshotData;
  auto snapshot = (const SnapshotData*)data.data();
  if (snapshot->type != SnapshotData::SnapshotType::kFlatBuffer ||
      snapshot->version != SnapshotData::SnapshotVersion::v1_0) {
    FXL_LOG(ERROR) << "Invalid snapshot format encountered. Aborting.";
    return;
  }

  auto flat_node = flatbuffers::GetRoot<snapshot::Node>(snapshot->data);
  LoadNode(parent_node(), flat_node);
}

void View::LoadNode(scenic::ContainerNode& parent_node,
                    const snapshot::Node* flat_node) {
  scenic::EntityNode entity_node(session());

  if (flat_node->transform()) {
    auto transform = flat_node->transform();
    entity_node.SetTranslation(transform->translation()->x(),   // x
                               transform->translation()->y(),   // y
                               transform->translation()->z());  // z
    entity_node.SetScale(transform->scale()->x(),               // x
                         transform->scale()->y(),               // y
                         transform->scale()->z());              // z
    entity_node.SetRotation(transform->rotation()->x(),         // x
                            transform->rotation()->y(),         // y
                            transform->rotation()->z(),         // z
                            transform->rotation()->w());        // w
    entity_node.SetAnchor(transform->anchor()->x(),             // x
                          transform->anchor()->y(),             // y
                          transform->anchor()->z());            // z
  }

  if (flat_node->shape()) {
    LoadShape(entity_node, flat_node);
  }

  parent_node.AddChild(entity_node);

  if (flat_node->children()) {
    for (auto child : *flat_node->children()) {
      LoadNode(entity_node, child);
    }
  }
}

void View::LoadShape(scenic::EntityNode& parent_node,
                     const snapshot::Node* flat_node) {
  scenic::ShapeNode shape_node(session());

  switch (flat_node->shape_type()) {
    case snapshot::Shape_NONE:
      return;
    case snapshot::Shape_Mesh:
      // TODO(sanjayc): Implement mesh.
      return;

    case snapshot::Shape_Circle: {
      auto shape = static_cast<const snapshot::Circle*>(flat_node->shape());
      scenic::Circle circle(session(), shape->radius());
      shape_node.SetShape(circle);
    } break;

    case snapshot::Shape_Rectangle: {
      auto shape = static_cast<const snapshot::Rectangle*>(flat_node->shape());
      scenic::Rectangle rectangle(session(),
                                  shape->width(),    // width
                                  shape->height());  // height
      shape_node.SetShape(rectangle);
    } break;

    case snapshot::Shape_RoundedRectangle: {
      auto shape =
          static_cast<const snapshot::RoundedRectangle*>(flat_node->shape());
      scenic::RoundedRectangle rounded_rectangle(
          session(),
          shape->width(),                // width
          shape->height(),               // height
          shape->top_left_radius(),      // top-left
          shape->top_right_radius(),     // top-right
          shape->bottom_right_radius(),  // bottom-right
          shape->bottom_left_radius());  // bottom-left
      shape_node.SetShape(rounded_rectangle);
    } break;

    default:
      // TODO(SCN-978): Return an error to the caller for invalid data.
      FXL_DCHECK(false) << "Unknown node type encountered. Corrupt flatbuffer?";
      return;
  }

  if (flat_node->material()) {
    LoadMaterial(shape_node, flat_node);
  }

  parent_node.AddPart(shape_node);
}

void View::LoadMaterial(scenic::ShapeNode& shape_node,
                        const snapshot::Node* flat_node) {
  if (flat_node->material_type() == snapshot::Material_Color) {
    auto color = static_cast<const snapshot::Color*>(flat_node->material());

    scenic::Material material(session());
    material.SetColor(255 * color->red(),     // red
                      255 * color->green(),   // green
                      255 * color->blue(),    // blue
                      255 * color->alpha());  // alpha
    shape_node.SetMaterial(material);
  } else if (flat_node->material_type() == snapshot::Material_Image) {
    auto image = static_cast<const snapshot::Image*>(flat_node->material());

    scenic::HostMemory memory(session(), image->data()->Length());
    memcpy(memory.data_ptr(), image->data()->Data(), image->data()->Length());

    // Create an ImageInfo to wrap the memory.
    fuchsia::images::ImageInfo image_info;
    image_info.width = image->width();
    image_info.height = image->height();
    const size_t kBytesPerPixel = 4u;
    image_info.stride = image->width() * kBytesPerPixel;
    image_info.pixel_format = fuchsia::images::PixelFormat::BGRA_8;
    image_info.color_space = fuchsia::images::ColorSpace::SRGB;
    image_info.tiling = fuchsia::images::Tiling::LINEAR;
    scenic::HostImage host_image(memory, 0, std::move(image_info));

    scenic::Material material(session());
    material.SetTexture(host_image.id());
    shape_node.SetMaterial(material);
  }
}

}  // namespace snapshot
