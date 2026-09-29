#include "Aporia/Scene/Scene.hpp"

#include <utility>

namespace Aporia {

ObjectHandle Scene::createObject(std::string name) {
    const ObjectHandle handle{nextId_++};
    objects_.push_back(Object{handle.value, std::move(name)});
    return handle;
}

Scene::Object* Scene::findObject(ObjectHandle handle) noexcept {
    if (!handle.valid()) {
        return nullptr;
    }

    for (Object& object : objects_) {
        if (object.id == handle.value) {
            return &object;
        }
    }

    return nullptr;
}

const Scene::Object* Scene::findObject(ObjectHandle handle) const noexcept {
    if (!handle.valid()) {
        return nullptr;
    }

    for (const Object& object : objects_) {
        if (object.id == handle.value) {
            return &object;
        }
    }

    return nullptr;
}

bool Scene::contains(ObjectHandle handle) const noexcept {
    return findObject(handle) != nullptr;
}

std::size_t Scene::objectCount() const noexcept {
    return objects_.size();
}

ComponentTypeId Scene::registerComponentType(std::string name) {
    if (name.empty()) {
        return invalidComponentType;
    }

    const auto existing = componentTypes_.find(name);
    if (existing != componentTypes_.end()) {
        return existing->second;
    }

    const ComponentTypeId type = nextComponentTypeId_++;
    componentTypes_.emplace(std::move(name), type);
    return type;
}

ComponentTypeId Scene::componentTypeId(const std::string& name) const {
    const auto existing = componentTypes_.find(name);
    return existing == componentTypes_.end() ? invalidComponentType : existing->second;
}

Scene::Component* Scene::findStoredComponent(ObjectHandle object, ComponentTypeId type) noexcept {
    if (!object.valid() || type == invalidComponentType) {
        return nullptr;
    }

    for (Component& component : components_) {
        if (component.objectId == object.value && component.type == type) {
            return &component;
        }
    }

    return nullptr;
}

const Scene::Component* Scene::findStoredComponent(ObjectHandle object, ComponentTypeId type) const noexcept {
    if (!object.valid() || type == invalidComponentType) {
        return nullptr;
    }

    for (const Component& component : components_) {
        if (component.objectId == object.value && component.type == type) {
            return &component;
        }
    }

    return nullptr;
}

bool Scene::containsComponent(ObjectHandle object, ComponentTypeId type) const noexcept {
    return findStoredComponent(object, type) != nullptr;
}

std::size_t Scene::componentCount(ObjectHandle object) const noexcept {
    if (!object.valid()) {
        return 0;
    }

    std::size_t count = 0;
    for (const Component& component : components_) {
        if (component.objectId == object.value) {
            ++count;
        }
    }
    return count;
}

bool Scene::removeComponent(ObjectHandle object, ComponentTypeId type) noexcept {
    Component* component = findStoredComponent(object, type);
    if (component == nullptr) {
        return false;
    }

    for (auto iterator = components_.begin(); iterator != components_.end(); ++iterator) {
        if (&*iterator == component) {
            components_.erase(iterator);
            return true;
        }
    }

    return false;
}

std::vector<Scene::ComponentView> Scene::components() const {
    std::vector<ComponentView> result;
    result.reserve(components_.size());
    for (const Component& component : components_) {
        result.push_back(ComponentView{component.objectId, component.type});
    }
    return result;
}

}
