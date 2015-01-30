#include <algorithm>
#include "scene.h"
#include "external/SOIL/SOIL.h"
#include "renderer/pipeline.h"
#include "shader/shaderpoll.h"
#include "base/ogldev_math_3d.h"
#include "base/node.h"
#include "geometry/mesh.h"
#include "material/materialpool.h"
Scene * Scene::currentScene = NULL;
Scene::Scene()
{
    initializeOpenGLFunctions();
    this->shadowMapFbo = new ShadowMapFBO();
    this->shadowMapFbo->Init(1024,768);
    this->setSkyBox(NULL);
    m_root.setName ("root");
    this->setRenderType (FORWARD_SHADING);
    m_GBuffer = new GBuffer();
    m_GBuffer->Init (1024,768);

    bloom_fbo1 = new RenderBuffer(1024,768);
    bloom_fbo2 = new RenderBuffer(1024,768);
    bloom_fbo3 = new RenderBuffer(1024,768);
    TMesh * mesh = new TMesh();
    mesh->pushVertex (VertexData(QVector3D(-1,-1,0),QVector2D(0,0)));
    mesh->pushVertex (VertexData(QVector3D(1,-1,0),QVector2D(1,0)));
    mesh->pushVertex (VertexData(QVector3D(1,1,0),QVector2D(1,1)));
    mesh->pushVertex (VertexData(QVector3D(-1,1,0),QVector2D(0,1)));
    mesh->pushIndex (0);
    mesh->pushIndex (1);
    mesh->pushIndex (2);
    mesh->pushIndex (0);
    mesh->pushIndex (2);
    mesh->pushIndex (3);
    mesh->setMaterial (MaterialPool::getInstance ()->createOrGetMaterial ("default"));
    mesh->finishWithoutNormal ();
    m_quad = new Entity();
    m_quad->addMesh (mesh);
    m_quad->setShaderProgram (ShaderPool::getInstance ()->get("dir_light_pass"));


    m_sphere = new Entity("res/model/built-in/sphere.obj");
    m_sphere->setShaderProgram (ShaderPool::getInstance ()->get ("point_light_pass"));
    m_guiCamera = new Camera();
    m_guiCamera->setOrtho (0,1024,0,768,0.01,1000);
    m_guiCamera->setPos (QVector3D(0,0,0));
}

void Scene::pushEntityToRenderQueue(Entity *entity)
{
    this->m_entityList.push_back(entity);
}

void Scene::pushSpriteToRenderQueue(Sprite *sprite)
{
    this->m_spriteList.push_back (sprite);
}

void Scene::render()
{
    this->m_root.visit (this);
    sortRenderQue ();
    switch(m_renderType)
    {
    case FORWARD_SHADING:
        forwardRendering ();
        break;
    case DEFERRED_SHADING:
        deferredRendering ();
        break;
    }
    spriteRenderPass ();
    m_tempEntityList = m_entityList;
    m_entityList.clear ();
}

DirectionalLight *Scene::getDirectionalLight()
{
    return & this->directionLight;
}

AmbientLight *Scene::getAmbientLight()
{
    return & this->ambientLight;
}

SpotLight *Scene::createSpotLight()
{
    SpotLight * spot_light = new SpotLight();
    this->spotLights.push_back(spot_light);
    return spot_light;
}

PointLight *Scene::createPointLight()
{
    PointLight * point_light = new PointLight();
    this->pointLights.push_back(point_light);
    return point_light;
}

void Scene::setAsCurrentScene()
{
    Scene::currentScene = this;
}



Scene *Scene::getCurrentScene()
{
    return Scene::currentScene;
}

SkyBox *Scene::skyBox() const
{
    return m_skyBox;
}

void Scene::setSkyBox(SkyBox *skyBox)
{
    m_skyBox = skyBox;
}

void Scene::setEntityBoneTransform(Entity *entity)
{
    if(entity->hasAnimation())
    {
        entity->getShaderProgram()->setUniformInteger("g_has_animation",1);
        std::vector <Matrix4f > transform;
        entity->bonesTransform(entity->animateTime(),transform,entity->animationName());
        for(int i =0;i<transform.size();i++)
        {
            char str[100];
            sprintf(str,"g_bones[%d]",i);
            entity->getShaderProgram()->setUniformMat4v(str,(const GLfloat*)(transform[i].m),true);
        }
    }else{
        entity->getShaderProgram()->setUniformInteger("g_has_animation",0);
    }
}

void Scene::spriteRenderPass()
{
    PipeLine p;
    for (auto i =m_spriteList.begin();i!=m_spriteList.end();i++)
    {
        Sprite * sprite = (* i);
        sprite->shader ()->use ();
        Camera * camera =sprite->camera ();
        p.setProjectionMatrix(camera->getProjection());
        p.setViewMatrix(camera->getViewMatrix());
        p.setEyePosition (camera->pos ());
        p.setModelMatrix(sprite->getModelTrans());
        p.apply(sprite->shader());

        sprite->draw();
        if(sprite->onRender)
        {
            sprite->onRender(sprite,0);
        }
    }
    m_spriteList.clear ();
}

void Scene::shadowPass(SpotLight * light)
{
    // Clear color and depth buffer
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    this->shadowMapFbo->BindForWriting();
    glClear(GL_DEPTH_BUFFER_BIT);
    ShaderProgram * shadow_shader = ShaderPool::getInstance ()->get("shadow");
    PipeLine p;
    for (std::vector<Entity *>::iterator i =m_entityList.begin();i!=m_entityList.end();i++)
    {
        shadow_shader->use ();
        Entity * entity = (* i);
        if(!entity->isEnableShadow()) continue;
        Camera * camera =entity->getCamera();
        p.setModelMatrix(entity->getModelTrans());
        p.setProjectionMatrix(camera->getProjection());
        QMatrix4x4 view;
        view.setToIdentity();
        view.lookAt(light->getPos(),light->getPos()+light->getDirection(),QVector3D(0,1,0));
        p.setViewMatrix(view);
        ShaderProgram * tmp_shader = entity->getShaderProgram ();
        entity->setShaderProgram (shadow_shader);
        setEntityBoneTransform(entity);
        p.apply(shadow_shader);
        entity->draw();
        entity->setShaderProgram (tmp_shader);
    }
    glDepthMask(GL_FALSE);
    glDisable(GL_DEPTH_TEST);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,0);//switch the frame buffer back.
}



void Scene::forwardRenderPass()
{
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    // Clear color and depth buffer
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    PipeLine p;
    for (std::vector<Entity *>::iterator i =m_entityList.begin();i!=m_entityList.end();i++)
    {
        Entity * entity = (* i);
        entity->getShaderProgram ()->use ();
        Camera * camera =entity->getCamera();
        p.setProjectionMatrix(camera->getProjection());
        p.setViewMatrix(camera->getViewMatrix());
        p.setEyePosition (camera->pos ());
        if(!spotLights.empty ())
        {
            QMatrix4x4 lightView;
            lightView.setToIdentity();
            lightView.lookAt(spotLights[0]->getPos(),this->spotLights[0]->getPos()+this->spotLights[0]->getDirection(),QVector3D(0,1,0));
            p.setLightViewMatrix(lightView);
        }
        p.setModelMatrix(entity->getModelTrans());
        p.applyLightMvp(entity->getShaderProgram());
        this->shadowMapFbo->applyShadowMapTexture(entity->getShaderProgram(),1);
        calculateLight(entity->getShaderProgram());
        setEntityBoneTransform(entity);
        p.apply(entity->getShaderProgram());
        entity->draw();
        if(entity->onRender)
        {
            entity->onRender(entity,0);
        }
    }
    if(m_skyBox)
    {
        Camera * camera =m_skyBox->camera();
        m_skyBox->shader()->use();
        p.setProjectionMatrix(camera->getProjection());
        p.setViewMatrix(camera->getViewMatrix());
        m_skyBox->getEntity()->translate(camera->pos ().x (),camera->pos ().y (),camera->pos ().z ());
        p.setModelMatrix(m_skyBox->getEntity()->getModelTrans());
        p.apply(m_skyBox->shader());
        m_skyBox->Draw();
    }
    glDepthMask(GL_FALSE);
    glDisable(GL_DEPTH_TEST);
}

void Scene::geometryPass()
{
    m_GBuffer->BindForWriting ();
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    PipeLine p;
    for (std::vector<Entity *>::iterator i =m_entityList.begin();i!=m_entityList.end();i++)
    {
        Entity * entity = (* i);
        entity->getShaderProgram ()->use ();
        Camera * camera =entity->getCamera();
        p.setProjectionMatrix(camera->getProjection());
        p.setViewMatrix(camera->getViewMatrix());
        p.setEyePosition (camera->pos ());
        p.setModelMatrix(entity->getModelTrans());
        p.applyLightMvp(entity->getShaderProgram());
        setEntityBoneTransform(entity);
        p.apply(entity->getShaderProgram());
        entity->draw();
        if(entity->onRender)
        {
            entity->onRender(entity,0);
        }
    }

    if(m_skyBox)
    {
        m_skyBox->setShader (ShaderPool::getInstance ()->get ("sky_box_deferred"));
        Camera * camera =m_skyBox->camera();
        m_skyBox->shader()->use();
        p.setProjectionMatrix(camera->getProjection());
        p.setViewMatrix(camera->getViewMatrix());
        m_skyBox->getEntity()->translate(camera->pos ().x (),camera->pos ().y (),camera->pos ().z ());
        p.setModelMatrix(m_skyBox->getEntity()->getModelTrans());
        p.apply(m_skyBox->shader());
        m_skyBox->Draw();
    }
    glDepthMask(GL_FALSE);
    glDisable(GL_DEPTH_TEST);
}

void Scene::lightPass()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    pointLightPass();
    spotLightPass();
    directionLightPass();

}

void Scene::spotLightPass()
{
    this->shadowMapFbo->BindForReading(GL_TEXTURE3);
    if(!this->spotLights.empty ())
    {
        for(int i =0;i<spotLights.size ();i++)
        {
            ShaderProgram * shader = ShaderPool::getInstance ()->get("spot_light_pass");
            shader->use ();
            shadowMapFbo->applyShadowMapTexture (shader,3);
            PointLight * light = this->spotLights[i];
            light->apply (shader,0);
            m_quad->setShaderProgram (shader);
            QMatrix4x4 m;
            m.setToIdentity ();
            shader->setUniformMat4v ("g_MVP_matrix",m.data ());
            shader->setUniform2Float ("g_screen_size",1024,768);
            shader->setUniformInteger ("g_color_map",0);
            shader->setUniformInteger ("g_position_map",1);
            shader->setUniformInteger ("g_normal_map",2);
            shader->setUniform3Float ("g_eye_position",
                                      m_camera->pos ().x(),
                                      m_camera->pos ().y(),
                                      m_camera->pos ().z());
            QMatrix4x4 lightView;
            lightView.setToIdentity();
            lightView.lookAt(spotLights[0]->getPos(),this->spotLights[0]->getPos()+this->spotLights[0]->getDirection(),QVector3D(0,1,0));
            QMatrix4x4 light_vp;
            light_vp = m_camera->getProjection () * lightView ;
            shader->setUniformMat4v ("g_light_vp_matrix",light_vp.data ());
            m_quad->draw (true);
        }
    }
}

void Scene::pointLightPass()
{
    if(!this->pointLights.empty ())
    {
        for(int i =0;i<pointLights.size ();i++)
        {
            ShaderProgram * shader = ShaderPool::getInstance ()->get("point_light_pass");
            shader->use ();
            PointLight * light = this->pointLights[i];
            light->apply (shader,0);

            m_quad->setShaderProgram (shader);
            QMatrix4x4 m;
            m.setToIdentity ();
            shader->setUniformMat4v ("g_MVP_matrix",m.data ());
            shader->setUniform2Float ("g_screen_size",1024,768);
            shader->setUniformInteger ("g_color_map",0);
            shader->setUniformInteger ("g_position_map",1);
            shader->setUniformInteger ("g_normal_map",2);
            shader->setUniform3Float ("g_eye_position",
                                      m_camera->pos ().x(),
                                      m_camera->pos ().y(),
                                      m_camera->pos ().z());

            m_quad->draw (true);
        }
    }
}

void Scene::directionLightPass()
{
    ShaderProgram * shader =ShaderPool::getInstance ()->get("dir_light_pass");
    shader->use ();
    QMatrix4x4 m;
    m.setToIdentity ();
    m_quad->setShaderProgram (shader);
    shader->setUniformMat4v ("g_MVP_matrix",m.data ());
    shader->setUniform2Float ("g_screen_size",1024,768);
    shader->setUniformInteger ("g_color_map",0);
    shader->setUniformInteger ("g_position_map",1);
    shader->setUniformInteger ("g_normal_map",2);
    shader->setUniform3Float ("g_eye_position",
                              m_camera->pos ().x(),
                              m_camera->pos ().y(),
                              m_camera->pos ().z());
    this->directionLight.apply(shader);
    this->ambientLight.apply(shader);
    m_quad->draw (true);
}

void Scene::calculateLight(ShaderProgram *shader)
{
    shader->setUniformInteger("g_point_light_amount",this->pointLights.size());
    shader->setUniformInteger("g_spot_light_amount",this->spotLights.size());
    this->directionLight.apply(shader);
    this->ambientLight.apply(shader);
    for(int j = 0;j<this->pointLights.size();j++)
    {
        this->pointLights[j]->apply(shader,j);
    }

    for(int k = 0;k<this->spotLights.size();k++)
    {
        this->spotLights[k]->apply(shader,k);
    }
}
#define WINDOW_WIDTH 1024
#define WINDOW_HEIGHT 768
void Scene::deferredRendering()
{
    if(!this->spotLights.empty ())
    {
        this->shadowPass(spotLights[0]);
    }
    geometryPass();
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_ONE, GL_ONE);
    m_GBuffer->BindForReading(bloom_fbo1->buffer ());

    lightPass();
    bloom_fbo1->BindForReading (bloom_fbo2);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    pickBright();
    bloom_fbo2->BindForReading (bloom_fbo3);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    gaussianBlur_H (2);
    bloom_fbo3->BindForReading (NULL);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    gaussianBlur_V (2);

    bloom_fbo1->BindForReading (NULL);

    //glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    ShaderProgram * shader =ShaderPool::getInstance ()->get("deffered_simple");
    shader->use ();
    QMatrix4x4 m;
    m.setToIdentity ();
    m_quad->setShaderProgram (shader);
    shader->setUniformMat4v ("g_MVP_matrix",m.data ());
    shader->setUniform2Float ("g_screen_size",1024,768);
    shader->setUniformInteger ("g_color_map",0);
    shader->setUniformInteger ("g_position_map",1);
    shader->setUniformInteger ("g_normal_map",2);
    shader->setUniform3Float ("g_eye_position",
                              m_camera->pos ().x(),
                              m_camera->pos ().y(),
                              m_camera->pos ().z());
    m_quad->draw (true);
}

void Scene::forwardRendering()
{

    if(!this->spotLights.empty ())
    {
        this->shadowPass(spotLights[0]);
    }
    this->shadowMapFbo->BindForReading(GL_TEXTURE1);
    this->forwardRenderPass();
}

Node *Scene::root()
{
    return &m_root;
}
int Scene::renderType() const
{
    return m_renderType;
}

void Scene::setRenderType(int renderType)
{
    m_renderType = renderType;
}

Camera *Scene::camera() const
{
    return m_camera;
}

void Scene::setCamera(Camera *camera)
{
    m_camera = camera;
}
Camera *Scene::guiCamera() const
{
    return m_guiCamera;
}

void Scene::setGuiCamera(Camera *guiCamera)
{
    m_guiCamera = guiCamera;
}

std::vector<Entity *> Scene::getPotentialEntityList() const
{
    return m_tempEntityList;
}

bool entityCompare( Entity* pfirst, Entity* psecond)
{
 return pfirst->getDistToCamera ()<psecond->getDistToCamera ();
}

void Scene::sortRenderQue()
{
    std::sort(m_entityList.begin (),m_entityList.end (),entityCompare);
}


void Scene::pickBright()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    ShaderProgram * shader =ShaderPool::getInstance ()->get("pick_bright");
    shader->use ();
    QMatrix4x4 m;
    m.setToIdentity ();
    m_quad->setShaderProgram (shader);
    shader->setUniformMat4v ("g_MVP_matrix",m.data ());
    shader->setUniform2Float ("g_screen_size",1024,768);
    shader->setUniformInteger ("g_color_map",0);
    shader->setUniformInteger ("g_position_map",1);
    shader->setUniformInteger ("g_normal_map",2);
    shader->setUniform3Float ("g_eye_position",
                              m_camera->pos ().x(),
                              m_camera->pos ().y(),
                              m_camera->pos ().z());
    this->directionLight.apply(shader);
    this->ambientLight.apply(shader);
    m_quad->draw (true);



}

void Scene::gaussianBlur_H(float size)
{

    ShaderProgram * shader =ShaderPool::getInstance ()->get("gaussian_blur");
    shader->use ();
    QMatrix4x4 m;
    m.setToIdentity ();
    m_quad->setShaderProgram (shader);
    shader->setUniformFloat ("g_blur_size",size);
    shader->setUniformMat4v ("g_MVP_matrix",m.data ());
    shader->setUniform2Float ("g_screen_size",1024,768);
    shader->setUniformInteger ("g_color_map",0);
    shader->setUniformInteger ("g_position_map",1);
    shader->setUniformInteger ("g_normal_map",2);
    shader->setUniform3Float ("g_eye_position",
                              m_camera->pos ().x(),
                              m_camera->pos ().y(),
                              m_camera->pos ().z());
    this->directionLight.apply(shader);
    this->ambientLight.apply(shader);
    m_quad->draw (true);
}

void Scene::gaussianBlur_V(float size)
{
    ShaderProgram * shader =ShaderPool::getInstance ()->get("gaussian_blur_v");
    shader->use ();
    QMatrix4x4 m;
    m.setToIdentity ();
    m_quad->setShaderProgram (shader);
    shader->setUniformFloat ("g_blur_size",size);
    shader->setUniformMat4v ("g_MVP_matrix",m.data ());
    shader->setUniform2Float ("g_screen_size",1024,768);
    shader->setUniformInteger ("g_color_map",0);
    shader->setUniformInteger ("g_position_map",1);
    shader->setUniformInteger ("g_normal_map",2);
    shader->setUniform3Float ("g_eye_position",
                              m_camera->pos ().x(),
                              m_camera->pos ().y(),
                              m_camera->pos ().z());
    this->directionLight.apply(shader);
    this->ambientLight.apply(shader);
    m_quad->draw (true);
}

void Scene::linearBlur(float radius, float samples)
{
    ShaderProgram * shader =ShaderPool::getInstance ()->get("linear_blur");
    shader->use ();
    QMatrix4x4 m;
    m.setToIdentity ();
    m_quad->setShaderProgram (shader);
    shader->setUniformFloat ("g_blur_radius",radius);
    shader->setUniformFloat ("g_blur_samples",samples);
    shader->setUniformMat4v ("g_MVP_matrix",m.data ());
    shader->setUniform2Float ("g_screen_size",1024,768);
    shader->setUniformInteger ("g_color_map",0);
    shader->setUniformInteger ("g_position_map",1);
    shader->setUniformInteger ("g_normal_map",2);
    shader->setUniform3Float ("g_eye_position",
                              m_camera->pos ().x(),
                              m_camera->pos ().y(),
                              m_camera->pos ().z());
    this->directionLight.apply(shader);
    this->ambientLight.apply(shader);
    m_quad->draw (true);
}

