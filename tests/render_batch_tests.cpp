#include "test_support.hpp"
#include "truffle/renderer/render_batch.hpp"

int main() {
    using namespace truffle::renderer;

    // --- InstanceLayout hash is deterministic ---
    InstanceLayout a;
    a.channels.push_back({ChannelKind::Transform, 0, 0, 64});
    a.bindingModel = BindingModel::Separate;

    InstanceLayout b;
    b.channels.push_back({ChannelKind::Transform, 0, 0, 64});
    b.bindingModel = BindingModel::Separate;

    TRUFFLE_CHECK(a.hash() == b.hash());
    TRUFFLE_CHECK(a == b);

    // --- Different channels produce different hashes ---
    InstanceLayout c;
    c.channels.push_back({ChannelKind::Transform, 0, 0, 64});
    c.channels.push_back({ChannelKind::Color,     1, 0, 16});
    c.bindingModel = BindingModel::Separate;

    TRUFFLE_CHECK(a.hash() != c.hash());
    TRUFFLE_CHECK(!(a == c));

    // --- Interleaved binding model produces a different hash ---
    InstanceLayout d;
    d.channels.push_back({ChannelKind::Transform, 0, 0, 80});
    d.channels.push_back({ChannelKind::Color,     0, 64, 80});
    d.bindingModel = BindingModel::Interleaved;

    TRUFFLE_CHECK(c.hash() != d.hash());

    // --- Hash stability: same object produces same hash twice ---
    TRUFFLE_CHECK(c.hash() == c.hash());

    // --- RenderBatch defaults are sensible ---
    RenderBatch batch;
    TRUFFLE_CHECK(batch.instanceCount == 0);
    TRUFFLE_CHECK(batch.vertexCount == 3);
    TRUFFLE_CHECK(batch.kind == DrawKind::Direct);
    TRUFFLE_CHECK(batch.material == 0);
    TRUFFLE_CHECK(batch.bindings[0].buffer == nullptr);

    // --- RenderBatch with data round-trips InstanceLayout ---
    batch.layout = c;
    TRUFFLE_CHECK(batch.layout == c);
    TRUFFLE_CHECK(batch.layout.hash() == c.hash());

    return 0;
}
