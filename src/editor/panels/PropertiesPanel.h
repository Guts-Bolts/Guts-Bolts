#pragma once
#include <functional>
#include <string>

class Scene;
class SceneNode;

// Inspector for the selected object's properties.
class PropertiesPanel {
public:
    PropertiesPanel(Scene* scene, std::function<void(SceneNode*)> openScript);
    void render();
    std::function<void(SceneNode*)> m_editMesh;   // "Edit Mesh" button: go into Modeling mode
    std::function<void()>           m_editCollisionGroups;   // "Edit..." next to Collision Group

private:
    void renderProperties(SceneNode* node);
    void renderGui(SceneNode* node);
    void renderAttributes(SceneNode* node);   // Attributes + Tags

    std::string m_newAttrName, m_newAttrError, m_newTag;
    int         m_newAttrType = 1;             // Number
    Scene*                          m_scene;
    std::function<void(SceneNode*)> m_openScript;
};
