/* SPDX-License-Identifier: GPL-3.0-only */
#include "render.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int rejected(const char *what,const Error *error)
{
    fprintf(stderr,"%s: %s\n",what,error->message);return 1;
}
int main(void)
{
    Config c={0};c.width=c.height=32;c.aa_scale=1;c.threads=1;c.sphere_scale=1;c.bond_radius=.12;
    for (int d=0;d<3;++d) c.repeat[d]=1;
    c.view=(Vec3){{1,1,1}};c.up=(Vec3){{0,0,1}};c.margin=.06;strcpy(c.background,"#ffffff");
    Atom atom={.element=6,.position={{0,0,0}}};Structure source={.atoms=&atom,.count=1};
    Scene scene={0};Camera camera={0};Error e={0};RenderWorkspace workspace={0};Image first={0},second={0};
    if (!scene_build(&c,&source,&scene,&e) || !camera_fit(&c,&scene,&camera,&e)) return rejected("ordinary scene",&e);
    if (!render_image_workspace(&c,&scene,&camera,sizeof atom,&workspace,&first,&e)) return rejected("first render",&e);
    double *depth=workspace.depth;uint8_t *rgba=workspace.rgba;
    if (!render_image_workspace(&c,&scene,&camera,sizeof atom,&workspace,&second,&e)) return rejected("second render",&e);
    if (workspace.depth!=depth || workspace.rgba!=rgba || first.pixels==second.pixels ||
        memcmp(first.pixels,second.pixels,first.width*first.height*4)) return rejected("sampling reuse or independent output ownership",&e);
    image_free(&first);image_free(&second);
    c.width=c.height=48;
    if (!camera_fit(&c,&scene,&camera,&e) || !render_image_workspace(&c,&scene,&camera,sizeof atom,&workspace,&first,&e) ||
        workspace.samples!=48u*48u) return rejected("workspace resize",&e);
    image_free(&first);render_workspace_free(&workspace);render_workspace_free(&workspace);
    if (workspace.depth || workspace.rgba || workspace.samples) return rejected("workspace cleanup",&e);
    scene_free(&scene);
    /* High retained data leaves enough room for required raster buffers, but
     * too little for many short outlined dashes. The uncached fallback must
     * still render precisely the same image, without allocating a large input. */
    c.width=c.height=64;c.cell=true;c.cell_style=CELL_DASHED;c.cell_width=2;
    c.cell_outline_width=1.5;c.cell_dash_length=c.cell_dash_gap=1;
    for (int d=0;d<3;++d) {source.lattice[d].x[d]=4;source.cell_present[d]=true;}
    if (!scene_build(&c,&source,&scene,&e) || !camera_fit(&c,&scene,&camera,&e) ||
        !render_image(&c,&scene,&camera,sizeof atom,&first,&e)) return rejected("cached short dashes",&e);
    size_t source_reservation=VR_MEMORY_BUDGET-8u*1024u*1024u-scene_memory_bytes(&scene)-c.width*c.height*16-scene.count*256-1024;
    if (!render_image(&c,&scene,&camera,source_reservation,&second,&e) ||
        memcmp(first.pixels,second.pixels,c.width*c.height*4)) return rejected("bounded dash-cache fallback",&e);
    image_free(&first);image_free(&second);scene_free(&scene);
    /* Reserve almost the whole budget without allocating it. The first scene
     * block must be rejected before realloc, including its growth allowance. */
    if (scene_build_retained(&c,&source,VR_MEMORY_BUDGET,&scene,&e) || e.code!=6 || scene.items) return rejected("retained scratch budget",&e);
    memset(&e,0,sizeof e);
    size_t scratch=sizeof(Vec3)+sizeof(Atom)+config_memory_bytes(&c);
    size_t extra=VR_MEMORY_BUDGET-scratch-255*sizeof(Primitive);
    if (scene_build_retained(&c,&source,extra,&scene,&e) || e.code!=6 || scene.items) return rejected("retained growth budget",&e);
    memset(&e,0,sizeof e);
    if (!scene_build_retained(&c,&source,extra-sizeof(Primitive),&scene,&e)) return rejected("exact scene budget",&e);
    scene_free(&scene);
    AtomRule rule={.element=6,.has_color=true,.color={255,0,0,255}};
    c.rule_count=1;c.rules=&rule;memset(&e,0,sizeof e);
    scratch=sizeof(Vec3)+sizeof(Atom)+config_memory_bytes(&c);
    if (scene_build_retained(&c,&source,VR_MEMORY_BUDGET-scratch-1,&scene,&e) || e.code!=6 || scene.items || scene.style_rules)
        return rejected("retained style table budget",&e);
    return 0;
}
