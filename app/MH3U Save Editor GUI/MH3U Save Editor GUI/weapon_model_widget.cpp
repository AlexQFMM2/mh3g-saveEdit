#include "weapon_model_widget.hpp"

#include <QFutureWatcher>
#include <QGridLayout>
#include <QLabel>
#include <QOpenGLContext>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QPushButton>
#include <QSurfaceFormat>
#include <QStyle>
#include <QTimer>
#include <QtConcurrent>

WeaponModelWidget::WeaponModelWidget(QWidget *parent)
    : QOpenGLWidget(parent), m_upright(false), m_request(0), m_glReady(false), m_gpuReady(false), m_cacheBytes(0),
      m_program(0), m_vertexBuffer(QOpenGLBuffer::VertexBuffer), m_indexBuffer(QOpenGLBuffer::IndexBuffer),
      m_yaw(-32.0f), m_pitch(18.0f), m_distance(3.0f)
{
    setObjectName("weaponModelWidget");
    setMinimumHeight(210);
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setSamples(4);
    setFormat(format);

    QGridLayout *overlay = new QGridLayout(this);
    overlay->setContentsMargins(5, 5, 5, 5);
    overlay->setHorizontalSpacing(0);
    overlay->setVerticalSpacing(0);
    overlay->setColumnStretch(1, 1);
    overlay->setRowStretch(1, 1);
    m_status = new QLabel(this);
    m_status->setObjectName("modelViewerStatus");
    m_status->setAlignment(Qt::AlignCenter);
    m_status->setWordWrap(true);
    m_status->setAttribute(Qt::WA_TransparentForMouseEvents);
    overlay->addWidget(m_status, 1, 1, Qt::AlignCenter);
    QPushButton *up = new QPushButton(QString::fromUtf8("↑"), this);
    QPushButton *down = new QPushButton(QString::fromUtf8("↓"), this);
    QPushButton *left = new QPushButton(QString::fromUtf8("←"), this);
    QPushButton *right = new QPushButton(QString::fromUtf8("→"), this);
    const QList<QPushButton *> directionButtons = QList<QPushButton *>() << up << down << left << right;
    for (QPushButton *button : directionButtons)
    {
        button->setObjectName("modelRotateButton");
        button->setFixedSize(28, 28);
        button->setAutoRepeat(true);
        button->setAutoRepeatDelay(350);
        button->setAutoRepeatInterval(90);
        button->setToolTip(QString::fromUtf8("点击或长按，按 15° 旋转模型"));
    }
    overlay->addWidget(up, 0, 1, Qt::AlignHCenter | Qt::AlignTop);
    overlay->addWidget(left, 1, 0, Qt::AlignLeft | Qt::AlignVCenter);
    overlay->addWidget(right, 1, 2, Qt::AlignRight | Qt::AlignVCenter);
    overlay->addWidget(down, 2, 1, Qt::AlignHCenter | Qt::AlignBottom);
    connect(up, SIGNAL(clicked()), this, SLOT(rotateUp()));
    connect(down, SIGNAL(clicked()), this, SLOT(rotateDown()));
    connect(left, SIGNAL(clicked()), this, SLOT(rotateLeft()));
    connect(right, SIGNAL(clicked()), this, SLOT(rotateRight()));
    setStatus(m_resources.statusText());
}

WeaponModelWidget::~WeaponModelWidget()
{
    makeCurrent();
    releaseGpu();
    delete m_program;
    m_program = 0;
    doneCurrent();
}

QString WeaponModelWidget::resourceStatus() const { return m_resources.statusText(); }

void WeaponModelWidget::setStatus(const QString &text, bool error)
{
    m_status->setText(text);
    m_status->setProperty("error", error);
    m_status->style()->unpolish(m_status);
    m_status->style()->polish(m_status);
    m_status->show();
}

void WeaponModelWidget::setModel(const QString &modelKey, const QString &arcRelativePath, bool upright)
{
    if (m_modelKey == modelKey && m_arcRelativePath == arcRelativePath && m_upright == upright
        && (m_model || !m_resources.available())) return;
    m_modelKey = modelKey;
    m_arcRelativePath = arcRelativePath;
    m_upright = upright;
    requestLoad();
}

void WeaponModelWidget::showItemPlaceholder()
{
    ++m_request;
    m_modelKey.clear(); m_arcRelativePath.clear(); m_model.clear();
    if (m_glReady) { makeCurrent(); releaseGpu(); doneCurrent(); }
    setStatus(QString::fromUtf8("道具详情暂不使用 3D 模型"));
    update();
}

void WeaponModelWidget::requestLoad()
{
    const int request = ++m_request;
    m_model.clear();
    if (m_glReady) { makeCurrent(); releaseGpu(); doneCurrent(); }
    if (m_modelKey.isEmpty() || m_arcRelativePath.isEmpty())
    { setStatus(QString::fromUtf8("该条目没有独立模型资源")); update(); return; }
    if (m_arcRelativePath.startsWith("armor-mod/") && !m_resources.armorAvailable())
    { setStatus(m_resources.statusText(m_arcRelativePath)); update(); return; }
    if (!m_resources.available())
    { setStatus(QString::fromUtf8("当前版本未包含 3D 模型资源\n请使用带 resources 目录的完整整合包")); update(); return; }
    const QSharedPointer<Mh3gCpuModel> cached = m_cache.value(m_modelKey);
    if (cached)
    { touchCache(m_modelKey, cached); acceptModel(request, cached); return; }
    const QString path = m_resources.archivePath(m_arcRelativePath);
    if (path.isEmpty()) { setStatus(QString::fromUtf8("整合包资源缺少 %1").arg(m_arcRelativePath), true); return; }
    setStatus(QString::fromUtf8("正在加载 %1…").arg(m_modelKey));
    QFutureWatcher<QSharedPointer<Mh3gCpuModel> > *watcher = new QFutureWatcher<QSharedPointer<Mh3gCpuModel> >(this);
    connect(watcher, &QFutureWatcher<QSharedPointer<Mh3gCpuModel> >::finished, this, [this, watcher, request]() {
        const QSharedPointer<Mh3gCpuModel> result = watcher->result();
        watcher->deleteLater();
        if (request != m_request) return;
        if (result && result->valid()) touchCache(result->modelKey, result);
        acceptModel(request, result);
    });
    const QString key = m_modelKey;
    watcher->setFuture(QtConcurrent::run([key, path]() { return Mh3gModelLoader::load(key, path); }));
}

void WeaponModelWidget::acceptModel(int request, const QSharedPointer<Mh3gCpuModel> &model)
{
    if (request != m_request) return;
    if (!model || !model->valid())
    { setStatus(QString::fromUtf8("模型解析失败\n%1").arg(model ? model->error : QString::fromUtf8("未知错误")), true); update(); return; }
    m_model = model;
    resetView();
    if (m_glReady) uploadModel();
    update();
}

void WeaponModelWidget::touchCache(const QString &key, const QSharedPointer<Mh3gCpuModel> &model)
{
    if (!m_cache.contains(key)) { m_cache.insert(key, model); m_cacheBytes += model->memoryBytes(); }
    m_lru.removeAll(key); m_lru.append(key); trimCache();
}

void WeaponModelWidget::trimCache()
{
    while ((m_cache.size() > 4 || m_cacheBytes > 128LL * 1024LL * 1024LL) && m_lru.size() > 1)
    {
        const QString key = m_lru.takeFirst();
        if (key == m_modelKey) { m_lru.append(key); continue; }
        m_cacheBytes -= m_cache.value(key)->memoryBytes(); m_cache.remove(key);
    }
}

void WeaponModelWidget::initializeGL()
{
    if (!initializeOpenGLFunctions() || !context() || context()->format().majorVersion() < 3
        || (context()->format().majorVersion() == 3 && context()->format().minorVersion() < 3))
    { setStatus(QString::fromUtf8("OpenGL 3.3 不可用，模型查看器已停用"), true); return; }
    m_program = new QOpenGLShaderProgram;
    const char *vertexShader =
        "#version 330 core\n"
        "layout(location=0) in vec3 position; layout(location=1) in vec3 normal; layout(location=2) in vec2 uv; layout(location=3) in vec4 tangent;\n"
        "uniform mat4 mvp; uniform mat4 modelView; out vec3 n; out vec3 tan; out float hand; out vec2 t; out vec3 p;\n"
        "void main(){ vec4 v=modelView*vec4(position,1.0); p=v.xyz; n=mat3(modelView)*normal; tan=mat3(modelView)*tangent.xyz; hand=tangent.w; t=uv; gl_Position=mvp*vec4(position,1.0); }";
    const char *fragmentShader =
        "#version 330 core\n"
        "in vec3 n; in vec3 tan; in float hand; in vec2 t; in vec3 p;\n"
        "uniform sampler2D albedoMap; uniform sampler2D normalMap; uniform sampler2D specularMap; uniform sampler2D environmentMap;\n"
        "uniform bool hasAlbedo; uniform bool hasNormal; uniform bool hasSpecular; uniform bool hasEnvironment; uniform bool useAlpha;\n"
        "uniform vec4 albedoFactor; uniform float specularStrength; uniform float roughness; uniform float environmentStrength; out vec4 color;\n"
        "void main(){ vec4 base=(hasAlbedo?texture(albedoMap,t):vec4(0.48,0.54,0.62,1.0))*albedoFactor; if(useAlpha&&base.a<0.08)discard;"
        "vec3 N=normalize(n); vec3 T=normalize(tan-N*dot(N,tan)); vec3 B=normalize(cross(N,T))*hand;"
        "if(hasNormal){ vec3 mapped=texture(normalMap,t).xyz*2.0-1.0; N=normalize(mat3(T,B,N)*mapped); }"
        "vec3 L=normalize(vec3(-0.35,0.72,0.60)); vec3 L2=normalize(vec3(0.55,0.25,0.80)); vec3 V=normalize(-p);"
        "float diffuse=0.22+0.62*max(dot(N,L),0.0)+0.16*max(dot(N,L2),0.0);"
        "float textureMask=hasSpecular?clamp(dot(texture(specularMap,t).rgb,vec3(0.3333)),0.0,1.0):base.a; float mask=specularStrength*textureMask;"
        "float shine=mix(88.0,12.0,roughness); float highlight=pow(max(dot(reflect(-L,N),V),0.0),shine)*mask*0.52;"
        "vec3 R=reflect(-V,N); vec2 euv=R.xy*0.34+vec2(0.5); vec3 env=hasEnvironment?pow(texture(environmentMap,euv).rgb,vec3(2.2)):vec3(0.08);"
        "vec3 baseLinear=pow(base.rgb,vec3(2.2)); vec3 linear=baseLinear*diffuse+env*environmentStrength+vec3(highlight);"
        "vec3 mapped=vec3(1.0)-exp(-linear*0.92); color=vec4(pow(clamp(mapped,0.0,1.0),vec3(1.0/2.2)),useAlpha?base.a:1.0); }";
    if (!m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShader)
        || !m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentShader) || !m_program->link())
    { setStatus(QString::fromUtf8("OpenGL 着色器初始化失败\n%1").arg(m_program->log()), true); return; }
    glEnable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    m_glReady = true;
    if (m_model && m_model->valid()) uploadModel();
}

void WeaponModelWidget::releaseGpu()
{
    m_gpuReady = false;
    for (GpuMaterial &material : m_gpuMaterials)
    {
        delete material.albedo;
        delete material.normal;
        delete material.specular;
        delete material.environment;
    }
    m_gpuMaterials.clear();
    if (m_vertexArray.isCreated()) m_vertexArray.destroy();
    if (m_vertexBuffer.isCreated()) m_vertexBuffer.destroy();
    if (m_indexBuffer.isCreated()) m_indexBuffer.destroy();
}

void WeaponModelWidget::uploadModel()
{
    if (!m_glReady || !m_model || !m_model->valid()) return;
    makeCurrent(); releaseGpu();
    QVector<float> packed;
    packed.reserve(m_model->vertices.size() * 12);
    for (const Mh3gVertex &vertex : m_model->vertices)
        packed << vertex.position.x() << vertex.position.y() << vertex.position.z()
               << vertex.normal.x() << vertex.normal.y() << vertex.normal.z() << vertex.uv.x() << vertex.uv.y()
               << vertex.tangent.x() << vertex.tangent.y() << vertex.tangent.z() << vertex.tangent.w();
    m_vertexArray.create(); m_vertexArray.bind();
    m_vertexBuffer.create(); m_vertexBuffer.bind();
    m_vertexBuffer.allocate(packed.constData(), packed.size() * int(sizeof(float)));
    m_indexBuffer.create(); m_indexBuffer.bind();
    m_indexBuffer.allocate(m_model->indices.constData(), m_model->indices.size() * int(sizeof(quint32)));
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 12 * sizeof(float), reinterpret_cast<void *>(0));
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 12 * sizeof(float), reinterpret_cast<void *>(3 * sizeof(float)));
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 12 * sizeof(float), reinterpret_cast<void *>(6 * sizeof(float)));
    glEnableVertexAttribArray(3); glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, 12 * sizeof(float), reinterpret_cast<void *>(8 * sizeof(float)));
    m_vertexArray.release();
    const auto createTexture = [](const QImage &image, QOpenGLTexture::WrapMode wrap) -> QOpenGLTexture *
    {
        if (image.isNull()) return 0;
        QOpenGLTexture *texture = new QOpenGLTexture(image);
        texture->setWrapMode(wrap);
        texture->setMinificationFilter(QOpenGLTexture::LinearMipMapLinear);
        texture->setMagnificationFilter(QOpenGLTexture::Linear);
        texture->generateMipMaps();
        return texture;
    };
    m_gpuMaterials.reserve(m_model->materials.size());
    for (const Mh3gMaterial &source : m_model->materials)
    {
        GpuMaterial material;
        material.albedo = createTexture(source.albedo, QOpenGLTexture::Repeat);
        material.normal = createTexture(source.normal, QOpenGLTexture::Repeat);
        material.specular = createTexture(source.specular, QOpenGLTexture::Repeat);
        material.environment = createTexture(source.environment, QOpenGLTexture::ClampToEdge);
        m_gpuMaterials.append(material);
    }
    m_gpuReady = true; m_status->hide(); doneCurrent();
}

void WeaponModelWidget::resizeGL(int, int) {}

void WeaponModelWidget::showEvent(QShowEvent *event)
{
    QOpenGLWidget::showEvent(event);
    QTimer::singleShot(500, this, [this]() {
        if (!isValid() && m_resources.available() && !m_modelKey.isEmpty())
            setStatus(QString::fromUtf8("OpenGL 3.3 不可用，模型查看器已停用\n图鉴和存档功能仍可正常使用"), true);
    });
}

void WeaponModelWidget::paintGL()
{
    glClearColor(0.91f, 0.94f, 0.97f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!m_glReady || !m_gpuReady || !m_model || !m_program) return;
    const QVector3D center = (m_model->boundsMinimum + m_model->boundsMaximum) * 0.5f;
    const QVector3D extent = m_model->boundsMaximum - m_model->boundsMinimum;
    const float radius = qMax(0.001f, extent.length() * 0.5f);
    QMatrix4x4 projection;
    projection.perspective(35.0f, float(qMax(1, width())) / float(qMax(1, height())), radius * 0.01f, radius * 100.0f);
    QMatrix4x4 view;
    view.translate(m_pan.x() * radius, m_pan.y() * radius, -m_distance * radius);
    view.rotate(m_pitch, 1, 0, 0); view.rotate(m_yaw, 0, 1, 0);
    if (!m_upright && extent.y() >= extent.x() && extent.y() >= extent.z()) view.rotate(-90.0f, 0, 0, 1);
    else if (!m_upright && extent.z() >= extent.x()) view.rotate(90.0f, 0, 1, 0);
    view.translate(-center);
    m_program->bind();
    m_program->setUniformValue("modelView", view);
    m_program->setUniformValue("mvp", projection * view);
    m_program->setUniformValue("albedoMap", 0);
    m_program->setUniformValue("normalMap", 1);
    m_program->setUniformValue("specularMap", 2);
    m_program->setUniformValue("environmentMap", 3);
    m_vertexArray.bind();
    const auto drawMaterial = [this](int materialIndex, int firstIndex, int indexCount) {
        const GpuMaterial empty;
        const GpuMaterial &material = materialIndex >= 0 && materialIndex < m_gpuMaterials.size()
            ? m_gpuMaterials[materialIndex] : empty;
        const Mh3gMaterial fallback;
        const Mh3gMaterial &source = materialIndex >= 0 && materialIndex < m_model->materials.size()
            ? m_model->materials[materialIndex] : fallback;
        m_program->setUniformValue("hasAlbedo", material.albedo != 0);
        m_program->setUniformValue("hasNormal", material.normal != 0);
        m_program->setUniformValue("hasSpecular", material.specular != 0);
        m_program->setUniformValue("hasEnvironment", material.environment != 0);
        m_program->setUniformValue("useAlpha", source.transparent);
        m_program->setUniformValue("albedoFactor", source.albedoFactor);
        m_program->setUniformValue("specularStrength", source.specularStrength);
        m_program->setUniformValue("roughness", source.roughness);
        m_program->setUniformValue("environmentStrength", source.environmentStrength);
        if (material.albedo) material.albedo->bind(0);
        if (material.normal) material.normal->bind(1);
        if (material.specular) material.specular->bind(2);
        if (material.environment) material.environment->bind(3);
        glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT,
            reinterpret_cast<const void *>(quintptr(firstIndex) * sizeof(quint32)));
        if (material.albedo) material.albedo->release(0);
        if (material.normal) material.normal->release(1);
        if (material.specular) material.specular->release(2);
        if (material.environment) material.environment->release(3);
    };
    if (m_model->drawCalls.isEmpty()) drawMaterial(0, 0, m_model->indices.size());
    else for (const Mh3gDrawCall &draw : m_model->drawCalls)
        drawMaterial(draw.materialIndex, draw.firstIndex, draw.indexCount);
    m_vertexArray.release();
    m_program->release();
}

void WeaponModelWidget::resetView()
{
    m_yaw = -32.0f; m_pitch = 18.0f; m_distance = 3.0f; m_pan = QVector3D(); update();
}

void WeaponModelWidget::rotateView(float yawDelta, float pitchDelta)
{
    m_yaw += yawDelta;
    while (m_yaw > 180.0f) m_yaw -= 360.0f;
    while (m_yaw < -180.0f) m_yaw += 360.0f;
    m_pitch = qBound(-89.0f, m_pitch + pitchDelta, 89.0f);
    update();
}

void WeaponModelWidget::rotateUp() { rotateView(0.0f, -15.0f); }
void WeaponModelWidget::rotateDown() { rotateView(0.0f, 15.0f); }
void WeaponModelWidget::rotateLeft() { rotateView(-15.0f, 0.0f); }
void WeaponModelWidget::rotateRight() { rotateView(15.0f, 0.0f); }
