    float2 screenUV=(i.uv-uvrect.xy)/uvrect.zw;
    if (post0.x > 0 && (post4.y == 0 || screenUV.x >= post4.z)) {
        float3 rgb=c.rgb;
        if(post1.w>0){
            float2 stepUV=1.0/float2(iw,ih);
            float3 blur=(img.SampleLevel(ls,clamp(i.uv+float2(stepUV.x,0),lo,hi),0).rgb+
                         img.SampleLevel(ls,clamp(i.uv-float2(stepUV.x,0),lo,hi),0).rgb+
                         img.SampleLevel(ls,clamp(i.uv+float2(0,stepUV.y),lo,hi),0).rgb+
                         img.SampleLevel(ls,clamp(i.uv-float2(0,stepUV.y),lo,hi),0).rgb)*.25;
            rgb+=post1.w*(rgb-blur);
        }
        // Clarity uses a broader neighborhood than fine sharpening.
        if(post2.y>0){
            float2 radius=abs(px)*3;
            float3 local=(img.SampleLevel(ls,clamp(i.uv+float2(radius.x,0),lo,hi),0).rgb+
                          img.SampleLevel(ls,clamp(i.uv-float2(radius.x,0),lo,hi),0).rgb+
                          img.SampleLevel(ls,clamp(i.uv+float2(0,radius.y),lo,hi),0).rgb+
                          img.SampleLevel(ls,clamp(i.uv-float2(0,radius.y),lo,hi),0).rgb)*.25;
            rgb+=(c.rgb-local)*post2.y;
        }
        if(post2.z>0){
            float3 glow=0;
            float2 radius=abs(px)*post3.x;
            // Two eight-tap rings give a soft halo without touching game targets.
            [unroll] for(int ring=0;ring<2;++ring){
                float ringWeight=ring==0?1-post3.y:post3.y;
                [unroll] for(int tap=0;tap<8;++tap){
                    float angle=tap*.7853981634;
                    float2 offset=float2(cos(angle),sin(angle))*radius*(ring==0?1:3);
                    float3 sampleRGB=img.SampleLevel(ls,clamp(i.uv+offset,lo,hi),0).rgb;
                    float peak=max(sampleRGB.r,max(sampleRGB.g,sampleRGB.b));
                    glow+=sampleRGB*max(peak-post2.w,0)/max(peak,.0001)*ringWeight*.125;
                }
            }
            rgb+=glow*post2.z;
        }
        float shadowWeight=1-saturate(dot(rgb,float3(.2126,.7152,.0722)));
        rgb+=post4.x*shadowWeight*shadowWeight;
        rgb=rgb*exp2(post0.y)+post0.z;
        rgb=(rgb-.5)*post0.w+.5;
        float luma=dot(rgb,float3(.2126,.7152,.0722));
        rgb=lerp(luma.xxx,rgb,post1.x);
        float chroma=saturate(max(rgb.r,max(rgb.g,rgb.b))-min(rgb.r,min(rgb.g,rgb.b)));
        rgb=lerp(luma.xxx,rgb,1+post3.w*(1-chroma));
        rgb*=float3(1+post1.z*.15,1,1-post1.z*.15);
        rgb=rgb/(1+post3.z*max(rgb-.5,0));
        rgb=pow(saturate(rgb),1/max(post1.y,.1));
        float2 centered=(i.uv-uvrect.xy)/uvrect.zw*2-1;
        rgb*=1-post2.x*saturate(dot(centered,centered)*.5);
        c.rgb=saturate(rgb);
    }
