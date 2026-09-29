#pragma once

#include "Aporia/Core/Export.hpp"

#include <any>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Aporia {

struct ObjectHandle {
    std::uint64_t value = 0;

    [[nodiscard]] constexpr bool valid() const noexcept {
        return value != 0;
    }

    explicit constexpr operator bool() const noexcept {
        return valid();
    }

    friend constexpr bool operator==(ObjectHandle, ObjectHandle) noexcept = default;
};

using ComponentTypeId = std::uint64_t;
inline constexpr ComponentTypeId invalidComponentType = 0;

class APORIA_API Scene {
public:
    ObjectHandle createObject(std::string name = {});

    [[nodiscard]] bool contains(ObjectHandle handle) const noexcept;
    [[nodiscard]] std::size_t objectCount() const noexcept;

    ComponentTypeId registerComponentType(std::string name);
    [[nodiscard]] ComponentTypeId componentTypeId(const std::string& name) const;

    template <typename T>
    bool addComponent(ObjectHandle object, ComponentTypeId type, T&& value) {
        return storeComponent(object, type, std::forward<T>(value));
    }

    template <typename T>
    [[nodiscard]] T* findComponent(ObjectHandle object, ComponentTypeId type) noexcept {
        Component* component = findStoredComponent(object, type);
        return component == nullptr ? nullptr : std::any_cast<T>(&component->value);
    }

    template <typename T>
    [[nodiscard]] const T* findComponent(ObjectHandle object, ComponentTypeId type) const noexcept {
        const Component* component = findStoredComponent(object, type);
        return component == nullptr ? nullptr : std::any_cast<T>(&component->value);
    }

    [[nodiscard]] bool containsComponent(ObjectHandle object, ComponentTypeId type) const noexcept;
    [[nodiscard]] std::size_t componentCount(ObjectHandle object) const noexcept;
    bool removeComponent(ObjectHandle object, ComponentTypeId type) noexcept;

    // Engine-internal iteration over stored components (Game::syncTransforms).
    struct ComponentView {
        std::uint64_t objectId;
        ComponentTypeId type;
    };
    [[nodiscard]] std::vector<ComponentView> components() const;

private:
    struct Object {
        std::uint64_t id;
        std::string name;
    };

    struct Component {
        std::uint64_t objectId;
        ComponentTypeId type;
        std::any value;
    };

    Object* findObject(ObjectHandle handle) noexcept;
    const Object* findObject(ObjectHandle handle) const noexcept;

    template <typename T>
    bool storeComponent(ObjectHandle object, ComponentTypeId type, T&& value) {
        if (type == invalidComponentType || findObject(object) == nullptr) {
            return false;
        }

        std::any any = std::forward<T>(value);
        if (!any.has_value()) {
            return false;
        }

        for (Component& component : components_) {
            if (component.objectId == object.value && component.type == type) {
                component.value = std::move(any);
                return true;
            }
        }

        components_.push_back(Component{object.value, type, std::move(any)});
        return true;
    }

    Component* findStoredComponent(ObjectHandle object, ComponentTypeId type) noexcept;
    const Component* findStoredComponent(ObjectHandle object, ComponentTypeId type) const noexcept;

    std::vector<Object> objects_;
    std::vector<Component> components_;
    std::unordered_map<std::string, ComponentTypeId> componentTypes_;
    std::uint64_t nextId_ = 1;
    ComponentTypeId nextComponentTypeId_ = 1;
};

}
