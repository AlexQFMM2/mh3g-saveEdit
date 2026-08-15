#include "weapon_model_widget.hpp"

#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QPushButton>
#include <QSurfaceFormat>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtConcurrent>

WeaponModelWidget::WeaponModelWidget(QWidget *parent)
    : QOpenGLWidget(parent), m_request(0), m_glReady(false), m_gpuReady(false), m_cacheBytes(0),
      m_program(0), m_vertexBuffer(QOpenGLBuffer::VertexBuffer), m_indexBuffer(QOpenGLBuffer::IndexBuffer),
      m_texture(0), m_environmentTexture(0), m_dragButton(Qt::NoButton), m_yaw(-32.0f), m_pitch(18.0f), m_distance(3.0f)
{
    setObjectName("weaponModelWidget");
    setMinimumHeight(210);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setSamples(4);
    setFormat(format);

    QVBoxLayout *overlay = new QVBoxLayout(this);
    overlay->setContentsMargins(8, 8, 8, 8);
    m_status = new QLabel(this);
    m_status->setObjectName("modelViewerStatus");
    m_status->setAlignment(Qt::AlignCenter);
    m_status->setWordWrap(true);
    m_status->setAttribute(Qt::WA_TransparentForMouseEvents);
    overlay->addWidget(m_status, 1);
    QHBoxLayout *buttons = new QHBoxLayout;
    m_reset = new QPushButton(QString::fromUtf8("重置视角"), this);
    buttons->addStretch();
    buttons->addWidget(m_reset);
    overlay->addLayout(buttons);
    connect(m_reset, SIGNAL(clicked()), this, SLOT(resetView()));
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

void WeaponModelWidget::setModel(const QString &modelKey, const QString &arcRelativePath)
{
    if (m_modelKey == modelKey && m_arcRelativePath == arcRelativePath && (m_model || !m_resources.available())) return;
    m_modelKey = modelKey;
    m_arcRelativePath = arcRelativePath;
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
    { setStatus(QString::fromUtf8("该武器的模型映射待确认")); update(); return; }
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
        "layout(location=0) in vec3 position; layout(location=1) in vec3 normal; layout(location=2) in vec2 uv;\n"
        "uniform mat4 mvp; uniform mat4 modelView; out vec3 n; out vec2 t; out vec3 p;\n"
        "void main(){ vec4 v=modelView*vec4(position,1.0); p=v.xyz; n=mat3(modelView)*normal; t=uv; gl_Position=mvp*vec4(position,1.0); }";
    const char *fragmentShader =
        "#version 330 core\n"
        "in vec3 n; in vec2 t; in vec3 p; uniform sampler2D diffuseMap; uniform sampler2D environmentMap; uniform bool hasTexture; uniform bool hasEnvironment; out vec4 color;\n"
        "void main(){ vec4 base=hasTexture?texture(diffuseMap,t):vec4(0.62,0.68,0.76,1.0); if(base.a<0.08)discard;"
        "vec3 N=normalize(n); vec3 L=normalize(vec3(-0.3,0.7,0.6)); vec3 V=normalize(-p);"
        "float d=max(dot(N,L),0.0); float s=pow(max(dot(reflect(-L,N),V),0.0),28.0);"
        "vec3 R=reflect(-V,N); vec2 euv=R.xy*0.35+vec2(0.5); vec3 env=hasEnvironment?texture(environmentMap,euv).rgb:vec3(0.35);"
        "color=vec4(base.rgb*(0.28+0.72*d)+env*(0.08+0.20*s),base.a); }";
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
    delete m_texture; m_texture = 0;
    delete m_environmentTexture; m_environmentTexture = 0;
    if (m_vertexArray.isCreated()) m_vertexArray.destroy();
    if (m_vertexBuffer.isCreated()) m_vertexBuffer.destroy();
    if (m_indexBuffer.isCreated()) m_indexBuffer.destroy();
}

void WeaponModelWidget::uploadModel()
{
    if (!m_glReady || !m_model || !m_model->valid()) return;
    makeCurrent(); releaseGpu();
    QVector<float> packed;
    packed.reserve(m_model->vertices.size() * 8);
    for (const Mh3gVertex &vertex : m_model->vertices)
        packed << vertex.position.x() << vertex.position.y() << vertex.position.z()
               << vertex.normal.x() << vertex.normal.y() << vertex.normal.z() << vertex.uv.x() << vertex.uv.y();
    m_vertexArray.create(); m_vertexArray.bind();
    m_vertexBuffer.create(); m_vertexBuffer.bind();
    m_vertexBuffer.allocate(packed.constData(), packed.size() * int(sizeof(float)));
    m_indexBuffer.create(); m_indexBuffer.bind();
    m_indexBuffer.allocate(m_model->indices.constData(), m_model->indices.size() * int(sizeof(quint32)));
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), reinterpret_cast<void *>(0));
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), reinterpret_cast<void *>(3 * sizeof(float)));
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), reinterpret_cast<void *>(6 * sizeof(float)));
    m_vertexArray.release();
    if (!m_model->diffuse.isNull())
    {
        m_texture = new QOpenGLTexture(m_model->diffuse);
        m_texture->setWrapMode(QOpenGLTexture::Repeat);
        m_texture->setMinificationFilter(QOpenGLTexture::LinearMipMapLinear);
        m_texture->setMagnificationFilter(QOpenGLTexture::Linear);
        m_texture->generateMipMaps();
    }
    if (!m_model->environment.isNull())
    {
        m_environmentTexture = new QOpenGLTexture(m_model->environment);
        m_environmentTexture->setWrapMode(QOpenGLTexture::ClampToEdge);
        m_environmentTexture->setMinificationFilter(QOpenGLTexture::LinearMipMapLinear);
        m_environmentTexture->setMagnificationFilter(QOpenGLTexture::Linear);
        m_environmentTexture->generateMipMaps();
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
    if (extent.y() >= extent.x() && extent.y() >= extent.z()) view.rotate(-90.0f, 0, 0, 1);
    else if (extent.z() >= extent.x()) view.rotate(90.0f, 0, 1, 0);
    view.translate(-center);
    m_program->bind();
    m_program->setUniformValue("modelView", view);
    m_program->setUniformValue("mvp", projection * view);
    m_program->setUniformValue("hasTexture", m_texture != 0);
    m_program->setUniformValue("hasEnvironment", m_environmentTexture != 0);
    m_program->setUniformValue("diffuseMap", 0);
    m_program->setUniformValue("environmentMap", 1);
    if (m_texture) m_texture->bind(0);
    if (m_environmentTexture) m_environmentTexture->bind(1);
    m_vertexArray.bind();
    glDrawElements(GL_TRIANGLES, m_model->indices.size(), GL_UNSIGNED_INT, 0);
    m_vertexArray.release();
    if (m_texture) m_texture->release(0);
    if (m_environmentTexture) m_environmentTexture->release(1);
    m_program->release();
}

void WeaponModelWidget::resetView()
{
    m_yaw = -32.0f; m_pitch = 18.0f; m_distance = 3.0f; m_pan = QVector3D(); update();
}

void WeaponModelWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton || event->button() == Qt::RightButton)
    { m_dragButton = event->button(); m_lastMouse = event->pos(); event->accept(); return; }
    QOpenGLWidget::mousePressEvent(event);
}

void WeaponModelWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragButton == Qt::NoButton) { QOpenGLWidget::mouseMoveEvent(event); return; }
    const QPoint delta = event->pos() - m_lastMouse; m_lastMouse = event->pos();
    if (m_dragButton == Qt::LeftButton) { m_yaw += delta.x() * 0.6f; m_pitch = qBound(-89.0f, m_pitch + delta.y() * 0.6f, 89.0f); }
    else { m_pan += QVector3D(delta.x() / float(qMax(1, width())) * 2.0f, -delta.y() / float(qMax(1, height())) * 2.0f, 0); }
    update(); event->accept();
}

void WeaponModelWidget::mouseDoubleClickEvent(QMouseEvent *event) { resetView(); event->accept(); }

void WeaponModelWidget::wheelEvent(QWheelEvent *event)
{
    m_distance = qBound(1.05f, m_distance * (event->angleDelta().y() > 0 ? 0.88f : 1.14f), 12.0f);
    update(); event->accept();
}
