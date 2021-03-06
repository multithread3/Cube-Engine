#pragma once
#include "rapidjson/document.h"
#include "GamePart.h"
namespace tzw
{
class vec3;
class Drawable3D;
class BlockPart;
struct Attachment;
class PhysicsHingeConstraint;
class LabelNew;
class GameConstraint : public GamePart
{
public:
	GameConstraint();
	Attachment * m_a;
	Attachment * m_b;
	virtual void enablePhysics(bool isEnable);
	virtual bool isEnablePhysics();
	virtual void dump(rapidjson::Value& partData, rapidjson::Document::AllocatorType& allocator);
	void load(rapidjson::Value& partData) override;
	bool isConstraint() override;
	void updateTransform(float dt);
	void setName(std::string newName) override;
	virtual ~GameConstraint();
	virtual void updateConstraintState();
	virtual void onUpdate(float dt);
protected:
	Matrix44 setUpFrameFromZ(vec3 pos, vec3 Zaixs, Matrix44 reservedMat);
	bool m_isEnablePhysics;
};
}
